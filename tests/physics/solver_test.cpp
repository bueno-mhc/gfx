// The sequential-impulse solver on its own: every joint row's Jacobian is the rate of its error (checked by moving the
// bodies a little), an elastic collision of equal bodies exchanges their velocities, a body hanging from a point stays.
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

/// @p frame moved for @p seconds by its velocities.
[[nodiscard]] auto
advanced(const ConstraintFrame& frame,
         float seconds) -> ConstraintFrame
{
    ConstraintFrame moved = frame;
    moved.center += frame.linear_velocity * seconds;
    const Vector3 turn = frame.angular_velocity * seconds;
    const float angle = length(turn);
    if (angle > 0.0F)
    {
        moved.orientation = normalized(Quaternion::from_axis_angle(turn / angle, angle) * frame.orientation);
    }
    return moved;
}

TEST(JointRowsTest, JacobiansAreTheRatesOfTheErrors)
{
    // Two bodies a little off their joint (turned, apart), moving in every direction: each row's error changes at the
    // rate its Jacobian gives.
    const ConstraintFrame first{.center = {.x = 0.1F, .y = 1.0F, .z = -0.2F},
                                .orientation = normalized(Quaternion::from_axis_angle(normalized(Vector3{.x = 1, .y = 2, .z = 3}), 0.2F)),
                                .linear_velocity = {.x = 0.3F, .y = -0.2F, .z = 0.5F},
                                .angular_velocity = {.x = 0.4F, .y = 0.7F, .z = -0.3F}};
    const ConstraintFrame second{.center = {.x = 1.2F, .y = 0.9F, .z = 0.1F},
                                 .orientation = normalized(Quaternion::from_axis_angle(normalized(Vector3{.x = -2, .y = 1, .z = 1}), 0.15F)),
                                 .linear_velocity = {.x = -0.4F, .y = 0.1F, .z = 0.2F},
                                 .angular_velocity = {.x = -0.5F, .y = 0.2F, .z = 0.6F}};
    const Transform frame_first{.position = {.x = 0.5F, .y = 0.1F}, .orientation = normalized(Quaternion::from_axis_angle({.y = 1}, 0.3F))};
    const Transform frame_second{.position = {.x = -0.55F, .y = 0.05F, .z = 0.02F}, .orientation = normalized(Quaternion::from_axis_angle({.y = 1}, 0.25F))};
    std::array<AxisMotion, 6> motion{AxisMotion::LOCKED, AxisMotion::LIMITED, AxisMotion::FREE, AxisMotion::LIMITED, AxisMotion::LOCKED, AxisMotion::LIMITED};
    std::array<JointLimits, 6> limits{};
    limits.fill({.lower = -0.5F, .upper = 0.5F});
    std::array<std::optional<JointMotor>, 6> motors{};
    motors[2] = JointMotor{.target_speed = 1.0F, .maximum_force = 10.0F};
    const std::vector<std::pair<const char*, JointDescription>> joints{
        {"rod", distance_joint({.length = 0.8F, .mode = DistanceMode::ROD})},
        {"spring", distance_joint({.length = 0.8F, .mode = DistanceMode::SPRING, .minimum_length = 0.5F, .maximum_length = 1.5F, .spring = {.frequency = 2.0F}})},
        {"rope", distance_joint({.length = 1.5F, .mode = DistanceMode::ROPE, .minimum_length = 0.2F})},
        {"stub", distance_joint({.mode = DistanceMode::STUB})},
        {"hinge", hinge_joint({.limits = JointLimits{.lower = -1.0F, .upper = 1.0F}, .motor = JointMotor{.target_speed = 1.0F, .maximum_force = 5.0F}, .spring = {.frequency = 1.0F}})},
        {"ball", ball_and_socket_joint({})},
        {"slider", slider_joint({.limits = JointLimits{.lower = -1.0F, .upper = 1.0F}, .spring = {.frequency = 1.0F}})},
        {"fixed", fixed_joint({})},
        {"cone-twist", cone_twist_joint({.swing_limit = 0.4F, .twist_limits = JointLimits{.lower = -0.3F, .upper = 0.3F}})},
        {"six-DOF", six_degrees_of_freedom_joint({.motion = motion, .limits = limits, .motors = motors})},
    };
    constexpr float SECONDS = 1.0e-3F;
    for (const auto& [name, description] : joints)
    {
        SCOPED_TRACE(name);
        const SolverJoint joint{.description = description, .frame_first = frame_first, .frame_second = frame_second};
        std::array<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> before{};
        std::array<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> after{};
        const std::uint32_t count = joint_rows(joint, first, second, before);
        ASSERT_EQ(joint_rows(joint, advanced(first, SECONDS), advanced(second, SECONDS), after), count) << "the same rows";
        ASSERT_GT(count, 0U);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const ConstraintRow& row = before[index];
            EXPECT_EQ(after[index].kind, row.kind) << index;
            if (row.kind == ConstraintRowKind::MOTOR)
            {
                continue;
            }
            const float rate = dot(row.linear_first, first.linear_velocity) + dot(row.angular_first, first.angular_velocity) + dot(row.linear_second, second.linear_velocity)
                               + dot(row.angular_second, second.angular_velocity);
            const float measured = (after[index].error - row.error) / SECONDS;
            EXPECT_NEAR(measured, rate, 0.02F + 0.02F * std::abs(rate)) << "row " << index;
        }
    }
}

TEST(SequentialImpulseSolverTest, AnElasticCollisionOfEqualBodiesExchangesTheirVelocities)
{
    // Two unit spheres touching head-on, one at rest: after the solve, the other is at rest and it moves on.
    std::array<SolverBody, 2> bodies{
        SolverBody{.center = {.x = -1.0F}, .linear_velocity = {.x = 2.0F}, .inverse_mass = 1.0F, .inverse_inertia = Matrix3::from_diagonal({.x = 2.5F, .y = 2.5F, .z = 2.5F})},
        SolverBody{.center = {.x = 1.0F}, .inverse_mass = 1.0F, .inverse_inertia = Matrix3::from_diagonal({.x = 2.5F, .y = 2.5F, .z = 2.5F})}};
    std::array<SolverContact, 1> contacts{SolverContact{.first = 0,
                                                        .second = 1,
                                                        .manifold = {.normal = {.x = 1.0F}, .points = {ContactPoint{.position = {}, .separation = 0.0F}}, .point_count = 1},
                                                        .restitution = 1.0F}};
    SequentialImpulseSolver solver;
    EXPECT_EQ(solver.kind(), SolverKind::SEQUENTIAL_IMPULSE);
    solver.solve({.seconds = 1.0F / 60.0F, .settings = {.restitution_threshold = 0.5F}, .bodies = bodies, .contacts = contacts});
    EXPECT_NEAR(bodies[0].linear_velocity.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(bodies[1].linear_velocity.x, 2.0F, 1.0e-4F);
    EXPECT_NEAR(contacts[0].manifold.points[0].normal_impulse, 2.0F, 1.0e-4F) << "the impulse kept for warm starting";

    // Bodies out of range are refused.
    contacts[0].second = 5;
    EXPECT_THROW(solver.solve({.seconds = 1.0F / 60.0F, .bodies = bodies, .contacts = contacts}), PhysicsError);
    EXPECT_THROW(solver.solve({.seconds = 0.0F, .bodies = bodies}), PhysicsError);
}

TEST(SequentialImpulseSolverTest, ABodyHangingFromAPointStaysAndItsJointCarriesItsWeight)
{
    // A body 1 m below a ball and socket to the world, under gravity applied before each solve: it stays, the joint
    // force is its weight.
    std::array<SolverBody, 1> bodies{SolverBody{.center = {.y = -1.0F}, .inverse_mass = 0.5F, .inverse_inertia = Matrix3::from_diagonal({.x = 1.0F, .y = 1.0F, .z = 1.0F})}};
    std::array<SolverJoint, 1> joints{SolverJoint{.first = 0, .second = NO_SOLVER_BODY, .description = ball_and_socket_joint({}), .frame_first = {.position = {.y = 1.0F}}}};
    SequentialImpulseSolver solver;
    constexpr float SECONDS = 1.0F / 60.0F;
    for (int step = 0; step < 120; ++step)
    {
        bodies[0].linear_velocity.y -= STANDARD_GRAVITY * SECONDS;
        solver.solve({.seconds = SECONDS, .bodies = bodies, .joints = joints});
    }
    EXPECT_NEAR(bodies[0].center.y, -1.0F, 1.0e-3F);
    EXPECT_NEAR(length(joints[0].force), 2.0F * STANDARD_GRAVITY, 0.01F * 2.0F * STANDARD_GRAVITY);
}

} // namespace
