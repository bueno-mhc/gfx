module gfx.physics.dynamics;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.interface;

namespace gfx::physics
{

namespace
{

/// Below it, a length counts as zero.
constexpr float TINY = 1.0e-9F;

/// The joint's two frames in the world, and what the rows are made from.
struct JointGeometry
{
    Quaternion frame_first{};  ///< The first body's joint frame (world orientation).
    Quaternion frame_second{}; ///< The second's.
    Quaternion relative{};     ///< The second frame in the first (the shorter way: w ≥ 0).
    Vector3 lever_first{};     ///< From the first center to its anchor.
    Vector3 lever_second{};    ///< From the second center to its anchor.
    Vector3 separation{};      ///< From the first anchor to the second.
};

[[nodiscard]] auto
geometry_of(const SolverJoint& joint,
            const ConstraintFrame& first,
            const ConstraintFrame& second) noexcept -> JointGeometry
{
    JointGeometry geometry{.frame_first = first.orientation * joint.frame_first.orientation, .frame_second = second.orientation * joint.frame_second.orientation};
    geometry.relative = conjugate(geometry.frame_first) * geometry.frame_second;
    if (geometry.relative.w < 0.0F)
    {
        geometry.relative = Quaternion{.x = -geometry.relative.x, .y = -geometry.relative.y, .z = -geometry.relative.z, .w = -geometry.relative.w};
    }
    geometry.lever_first = rotate(first.orientation, joint.frame_first.position);
    geometry.lever_second = rotate(second.orientation, joint.frame_second.position);
    geometry.separation = (second.center + geometry.lever_second) - (first.center + geometry.lever_first);
    return geometry;
}

/// The unit axis @p index (0 x, 1 y, 2 z) of the frame @p frame.
[[nodiscard]] auto
axis_of(const Quaternion& frame,
        std::size_t index) noexcept -> Vector3
{
    constexpr std::array<Vector3, 3> AXES{Vector3{.x = 1.0F}, Vector3{.y = 1.0F}, Vector3{.z = 1.0F}};
    return rotate(frame, AXES[index]);
}

/// The unit vector of axis @p index.
[[nodiscard]] auto
unit_of(std::size_t index) noexcept -> Vector3
{
    constexpr std::array<Vector3, 3> AXES{Vector3{.x = 1.0F}, Vector3{.y = 1.0F}, Vector3{.z = 1.0F}};
    return AXES[index];
}

/// The relative rotation's vector part.
[[nodiscard]] auto
vector_of(const Quaternion& rotation) noexcept -> Vector3
{
    return {.x = rotation.x, .y = rotation.y, .z = rotation.z};
}

/// The world axis along which the relative angular velocity (second minus first) changes 2·v[@p index], v the relative
/// rotation's vector part: with q̇ = ½ Ω ⊗ q (Ω in the first frame), d(2v)/dt = Ω · (w e + v × e).
[[nodiscard]] auto
lock_axis(const JointGeometry& geometry,
          std::size_t index) noexcept -> Vector3
{
    const Vector3 unit = unit_of(index);
    return rotate(geometry.frame_first, unit * geometry.relative.w + cross(vector_of(geometry.relative), unit));
}

/// The world axis along which the relative angular velocity changes the angle 2·atan2(v[@p index], w) (exactly; e for
/// a turn about axis @p index alone).
[[nodiscard]] auto
angle_axis(const JointGeometry& geometry,
           std::size_t index) noexcept -> Vector3
{
    const Vector3 unit = unit_of(index);
    const Vector3 vector = vector_of(geometry.relative);
    const float component = dot(vector, unit);
    const float w = geometry.relative.w;
    const float scale = component * component + w * w;
    if (!(scale > TINY))
    {
        return axis_of(geometry.frame_first, index);
    }
    return rotate(geometry.frame_first, ((unit * w + cross(vector, unit)) * w + vector * component) / scale);
}

/// The relative rotation's component @p index (x, y, z) as an angle about that axis (exact for a turn about it alone).
[[nodiscard]] auto
angle_about(const Quaternion& relative,
            std::size_t index) noexcept -> float
{
    const std::array<float, 3> components{relative.x, relative.y, relative.z};
    float angle = 2.0F * std::atan2(components[index], relative.w);
    constexpr float PI = std::numbers::pi_v<float>;
    if (angle > PI)
    {
        angle -= 2.0F * PI;
    }
    return angle;
}

/// Writes rows in order.
class RowWriter
{
public:
    RowWriter(std::span<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> rows,
              const JointGeometry& geometry) noexcept
        : m_rows{rows}
        , m_geometry{&geometry}
    {
    }

    [[nodiscard]] auto count() const noexcept -> std::uint32_t
    {
        return m_count;
    }

    /// Keeps the anchors' separation along @p direction at 0 (a point's row, levers from each center).
    void point(Vector3 direction)
    {
        add({.kind = ConstraintRowKind::EQUALITY,
             .linear_first = -direction,
             .angular_first = -cross(m_geometry->lever_first, direction),
             .linear_second = direction,
             .angular_second = cross(m_geometry->lever_second, direction),
             .error = dot(m_geometry->separation, direction)});
    }

    /// A row along @p direction for a translation measured in the first frame (the first lever reaches the second
    /// anchor): @p sign −1 reverses it (an upper limit).
    void slide(ConstraintRowKind kind,
               Vector3 direction,
               float error,
               float sign = 1.0F)
    {
        const Vector3 along = direction * sign;
        add({.kind = kind,
             .linear_first = -along,
             .angular_first = -cross(m_geometry->lever_first + m_geometry->separation, along),
             .linear_second = along,
             .angular_second = cross(m_geometry->lever_second, along),
             .error = error});
    }

    /// A row about @p axis (the second body turning about it relative to the first): @p sign −1 reverses it.
    void turn(ConstraintRowKind kind,
              Vector3 axis,
              float error,
              float sign = 1.0F)
    {
        add({.kind = kind, .linear_first = {}, .angular_first = axis * -sign, .linear_second = {}, .angular_second = axis * sign, .error = error});
    }

    /// The last row added becomes a motor of @p motor.
    void as_motor(const JointMotor& motor)
    {
        ConstraintRow& row = m_rows[m_count - 1];
        row.kind = ConstraintRowKind::MOTOR;
        row.speed = motor.target_speed;
        row.maximum_force = motor.maximum_force;
    }

    /// The last row added becomes soft (a spring of @p spring) unless its frequency is 0.
    void as_spring(const JointSpring& spring)
    {
        if (spring.frequency > 0.0F)
        {
            ConstraintRow& row = m_rows[m_count - 1];
            row.kind = ConstraintRowKind::SPRING;
            row.spring = spring;
        }
    }

private:
    void add(const ConstraintRow& row)
    {
        if (m_count < m_rows.size())
        {
            m_rows[m_count++] = row;
        }
    }

private:
    std::span<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> m_rows;
    const JointGeometry* m_geometry;
    std::uint32_t m_count = 0;
};

/// The three point rows (the anchors together).
void
hold_point(RowWriter& writer)
{
    writer.point({.x = 1.0F});
    writer.point({.y = 1.0F});
    writer.point({.z = 1.0F});
}

/// The rotation error of the second frame in the first, as a small rotation about each of the first frame's axes.
[[nodiscard]] auto
rotation_error(const JointGeometry& geometry,
               std::size_t index) noexcept -> float
{
    const std::array<float, 3> components{geometry.relative.x, geometry.relative.y, geometry.relative.z};
    return 2.0F * components[index];
}

/// The row that keeps the relative rotation's component @p index at 0.
void
lock_rotation(RowWriter& writer,
              const JointGeometry& geometry,
              std::size_t index)
{
    writer.turn(ConstraintRowKind::EQUALITY, lock_axis(geometry, index), rotation_error(geometry, index));
}

/// The three rows that keep the frames' orientations (each soft when @p spring has a frequency).
void
hold_orientation(RowWriter& writer,
                 const JointGeometry& geometry,
                 const JointSpring& spring = {})
{
    for (std::size_t index = 0; index < 3; ++index)
    {
        lock_rotation(writer, geometry, index);
        writer.as_spring(spring);
    }
}

/// Lower and upper limit rows about @p axis for @p angle.
void
angle_limits(RowWriter& writer,
             Vector3 axis,
             float angle,
             const JointLimits& limits)
{
    writer.turn(ConstraintRowKind::INEQUALITY, axis, angle - limits.lower);
    writer.turn(ConstraintRowKind::INEQUALITY, axis, limits.upper - angle, -1.0F);
}

/// Lower and upper limit rows along @p direction for the translation @p distance.
void
distance_limits(RowWriter& writer,
                Vector3 direction,
                float distance,
                const JointLimits& limits)
{
    writer.slide(ConstraintRowKind::INEQUALITY, direction, distance - limits.lower);
    writer.slide(ConstraintRowKind::INEQUALITY, direction, limits.upper - distance, -1.0F);
}

} // namespace

auto
joint_rows(const SolverJoint& joint,
           const ConstraintFrame& first,
           const ConstraintFrame& second,
           std::span<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> rows) noexcept -> std::uint32_t
{
    const JointGeometry geometry = geometry_of(joint, first, second);
    RowWriter writer{rows, geometry};
    std::visit(
        [&](const auto& description) {
            using Description = std::remove_cvref_t<decltype(description)>;
            if constexpr (std::same_as<Description, DistanceJointDescription>)
            {
                // A stub holds the points together; the others act along the line between them.
                if (description.mode == DistanceMode::STUB)
                {
                    hold_point(writer);
                    return;
                }
                const float distance = length(geometry.separation);
                const Vector3 direction = distance > TINY ? geometry.separation / distance : Vector3{.y = 1.0F};
                switch (description.mode)
                {
                    case DistanceMode::ROD:
                        writer.slide(ConstraintRowKind::EQUALITY, direction, distance - description.length);
                        break;
                    case DistanceMode::SPRING:
                        // A spring of frequency 0 pulls nothing: the length is free between the limits.
                        if (description.spring.frequency > 0.0F)
                        {
                            writer.slide(ConstraintRowKind::EQUALITY, direction, distance - description.length);
                            writer.as_spring(description.spring);
                        }
                        if (description.minimum_length > 0.0F)
                        {
                            writer.slide(ConstraintRowKind::INEQUALITY, direction, distance - description.minimum_length);
                        }
                        if (std::isfinite(description.maximum_length))
                        {
                            writer.slide(ConstraintRowKind::INEQUALITY, direction, description.maximum_length - distance, -1.0F);
                        }
                        break;
                    case DistanceMode::ROPE:
                        writer.slide(ConstraintRowKind::INEQUALITY, direction, description.length - distance, -1.0F);
                        if (description.minimum_length > 0.0F)
                        {
                            writer.slide(ConstraintRowKind::INEQUALITY, direction, distance - description.minimum_length);
                        }
                        break;
                    case DistanceMode::STUB:
                        break;
                }
            }
            else if constexpr (std::same_as<Description, HingeJointDescription>)
            {
                // The point; no turn about the frame's x and y; limits, motor, spring about z.
                hold_point(writer);
                const Vector3 axis = axis_of(geometry.frame_first, 2);
                lock_rotation(writer, geometry, 0);
                lock_rotation(writer, geometry, 1);
                const float angle = angle_about(geometry.relative, 2);
                if (description.limits)
                {
                    angle_limits(writer, angle_axis(geometry, 2), angle, *description.limits);
                }
                if (description.motor)
                {
                    writer.turn(ConstraintRowKind::MOTOR, axis, 0.0F);
                    writer.as_motor(*description.motor);
                }
                if (description.spring.frequency > 0.0F)
                {
                    writer.turn(ConstraintRowKind::EQUALITY, angle_axis(geometry, 2), angle);
                    writer.as_spring(description.spring);
                }
            }
            else if constexpr (std::same_as<Description, BallAndSocketJointDescription>)
            {
                hold_point(writer);
            }
            else if constexpr (std::same_as<Description, SliderJointDescription>)
            {
                // Across the axis (the frame's y and z) held, the orientation kept; limits, motor, spring along x.
                const Vector3 axis = axis_of(geometry.frame_first, 0);
                writer.slide(ConstraintRowKind::EQUALITY, axis_of(geometry.frame_first, 1), dot(geometry.separation, axis_of(geometry.frame_first, 1)));
                writer.slide(ConstraintRowKind::EQUALITY, axis_of(geometry.frame_first, 2), dot(geometry.separation, axis_of(geometry.frame_first, 2)));
                hold_orientation(writer, geometry);
                const float translation = dot(geometry.separation, axis);
                if (description.limits)
                {
                    distance_limits(writer, axis, translation, *description.limits);
                }
                if (description.motor)
                {
                    writer.slide(ConstraintRowKind::MOTOR, axis, 0.0F);
                    writer.as_motor(*description.motor);
                }
                if (description.spring.frequency > 0.0F)
                {
                    writer.slide(ConstraintRowKind::EQUALITY, axis, translation);
                    writer.as_spring(description.spring);
                }
            }
            else if constexpr (std::same_as<Description, FixedJointDescription>)
            {
                // The point and the orientation, each soft when its spring has a frequency.
                writer.point({.x = 1.0F});
                writer.as_spring(description.linear_spring);
                writer.point({.y = 1.0F});
                writer.as_spring(description.linear_spring);
                writer.point({.z = 1.0F});
                writer.as_spring(description.linear_spring);
                hold_orientation(writer, geometry, description.angular_spring);
            }
            else if constexpr (std::same_as<Description, ConeTwistJointDescription>)
            {
                // The point; the second frame's z within the cone around the first's; the twist about z within its limits.
                hold_point(writer);
                const Vector3 axis = axis_of(geometry.frame_first, 2);
                const Vector3 other = axis_of(geometry.frame_second, 2);
                const Vector3 bend = cross(axis, other);
                const float bend_size = length(bend);
                const float swing = std::atan2(bend_size, dot(axis, other));
                const Vector3 swing_axis = bend_size > TINY ? bend / bend_size : axis_of(geometry.frame_first, 0);
                writer.turn(ConstraintRowKind::INEQUALITY, swing_axis, description.swing_limit - swing, -1.0F);
                if (description.twist_limits)
                {
                    angle_limits(writer, angle_axis(geometry, 2), angle_about(geometry.relative, 2), *description.twist_limits);
                }
            }
            else if constexpr (std::same_as<Description, SixDegreesOfFreedomJointDescription>)
            {
                // Each axis of the first frame: locked (an equality), limited (two limits) or free; a motor on any.
                for (std::size_t index = 0; index < 6; ++index)
                {
                    const bool linear = index < 3;
                    const Vector3 axis = axis_of(geometry.frame_first, index % 3);
                    const float value = linear ? dot(geometry.separation, axis) : angle_about(geometry.relative, index - 3);
                    switch (description.motion[index])
                    {
                        case AxisMotion::LOCKED:
                            if (linear)
                            {
                                writer.slide(ConstraintRowKind::EQUALITY, axis, value);
                            }
                            else
                            {
                                lock_rotation(writer, geometry, index - 3);
                            }
                            break;
                        case AxisMotion::LIMITED:
                            if (linear)
                            {
                                distance_limits(writer, axis, value, description.limits[index]);
                            }
                            else
                            {
                                angle_limits(writer, angle_axis(geometry, index - 3), value, description.limits[index]);
                            }
                            break;
                        case AxisMotion::FREE:
                            break;
                    }
                    if (description.motors[index])
                    {
                        if (linear)
                        {
                            writer.slide(ConstraintRowKind::MOTOR, axis, 0.0F);
                        }
                        else
                        {
                            writer.turn(ConstraintRowKind::MOTOR, axis, 0.0F);
                        }
                        writer.as_motor(*description.motors[index]);
                    }
                }
            }
        },
        joint.description);
    return writer.count();
}

} // namespace gfx::physics
