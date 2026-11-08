// The conformance suite, joints and constraints: on every backend, a pendulum swings with its period, a hinge holds its
// limit, a rope its length, a weak weld breaks, a motor reaches its speed, a joint breaks under a shock whatever the
// sub-steps while a steady load reports its weight; where the backend has them, six-DOF joints keep their locked axes and
// user constraints hold their bodies and break.
#include "conformance_test.hpp"

namespace gfx::tests::conformance
{
namespace
{

using JointConformanceTest = ConformanceTest;

TEST_P(JointConformanceTest, APendulumSwingsWithItsPeriod)
{
    // A small heavy bob 1 m below a hinge to the world, released at 5 degrees, eight sub-steps: T = 2π √(L / g) within
    // 2 %, the hinge holding the distance within 1 cm.
    const std::unique_ptr<PhysicsWorld> made = world({.solver = {.sub_steps = 8}});
    constexpr float LENGTH = 1.0F;
    constexpr float START = 5.0F * PI / 180.0F;
    const Vector3 bob_position{.x = LENGTH * std::sin(START), .y = 2.0F - LENGTH * std::cos(START)};
    const Body bob = make_body(*made, {.pose = {.position = bob_position}, .angular_damping = 0.0F}, {.shape = sphere(0.02F), .density = 20000.0F});
    const std::unique_ptr<Joint> hinge = made->create_joint(hinge_joint({.first = bob.body->handle(), .anchor = {.y = 2.0F}, .axis = {.z = 1.0F}}));
    std::vector<float> crossings;
    float previous = bob_position.x;
    for (int step = 1; step <= 60 * 7; ++step)
    {
        made->step(STEP);
        const float x = bob.body->state().pose.position.x;
        if (previous > 0.0F && x <= 0.0F)
        {
            crossings.push_back(static_cast<float>(step) * STEP - STEP * x / (x - previous));
        }
        previous = x;
    }
    ASSERT_GE(crossings.size(), 3U);
    const float period = (crossings.back() - crossings.front()) / static_cast<float>(crossings.size() - 1);
    const float expected = 2.0F * PI * std::sqrt(LENGTH / STANDARD_GRAVITY);
    EXPECT_NEAR(period, expected, 0.02F * expected);
    EXPECT_NEAR(length(bob.body->state().pose.position - Vector3{.y = 2.0F}), LENGTH, 0.01F) << "the hinge holds the distance";
}

TEST_P(JointConformanceTest, JointsHoldTheirLimitsRopesTheirLengthAndWeldsBreak)
{
    const std::unique_ptr<PhysicsWorld> made = world();

    // An arm on a hinge limited to ±0.5 rad from horizontal falls to its lower limit.
    const Body arm = make_body(*made, {.pose = {.position = {.x = 1.0F, .y = 5.0F}}}, {.shape = box({.x = 1.0F, .y = 0.05F, .z = 0.05F})});
    const std::unique_ptr<Joint> hinge =
        made->create_joint(hinge_joint({.first = arm.body->handle(), .anchor = {.y = 5.0F}, .axis = {.z = 1.0F}, .limits = JointLimits{.lower = -0.5F, .upper = 0.5F}}));

    // A ball on a rope of 2 m from a point 10 m up falls to the rope's end.
    const Body ball = make_body(*made, {.pose = {.position = {.x = 5.0F, .y = 9.5F}}}, {.shape = sphere(0.1F)});
    const std::unique_ptr<Joint> rope = made->create_joint(
        distance_joint({.first = ball.body->handle(), .anchor_first = {}, .anchor_second = {.x = 5.0F, .y = 10.0F}, .length = 2.0F, .mode = DistanceMode::ROPE}));

    // A heavy block welded to the world with a weak weld breaks, and falls.
    const Body block = make_body(*made, {.pose = {.position = {.x = -5.0F, .y = 5.0F}}}, {.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .density = 5000.0F});
    const std::unique_ptr<Joint> weld = made->create_joint(fixed_joint({.first = block.body->handle(), .anchor = {.x = -5.0F, .y = 5.0F}, .breaking = {.force = 1000.0F}}));

    // A wheel on a hinge to the world with a motor spins up to its target speed (the second body — the world — relative to
    // the first: the wheel turns at minus it).
    const Body wheel = make_body(*made, {.pose = {.position = {.x = 10.0F, .y = 5.0F}}, .angular_damping = 0.0F}, {.shape = sphere(0.5F)});
    const std::unique_ptr<Joint> axle = made->create_joint(
        hinge_joint({.first = wheel.body->handle(), .anchor = {.x = 10.0F, .y = 5.0F}, .axis = {.z = 1.0F}, .motor = JointMotor{.target_speed = 3.0F, .maximum_force = 500.0F}}));

    bool broke = false;
    for (int step = 0; step < 180; ++step)
    {
        made->step(STEP);
        broke = broke || std::ranges::any_of(made->events().joints, [&weld](const JointEvent& event) { return event.joint == weld->handle(); });
    }
    const Vector3 tip = transform_point(arm.body->state().pose, Vector3{.x = 1.0F});
    EXPECT_NEAR(std::atan2(tip.y - 5.0F, tip.x), -0.5F, 0.05F) << "the lower limit";
    EXPECT_LE(length(ball.body->state().pose.position - Vector3{.x = 5.0F, .y = 10.0F}), 2.0F + 0.02F);
    EXPECT_GT(length(ball.body->state().pose.position - Vector3{.x = 5.0F, .y = 10.0F}), 1.9F);
    EXPECT_TRUE(broke);
    EXPECT_TRUE(weld->state().broken);
    EXPECT_LT(block.body->state().pose.position.y, 0.0F) << "it fell once free";
    EXPECT_NEAR(wheel.body->state().angular_velocity.z, -3.0F, 0.05F) << "the motor's speed";
    EXPECT_NEAR(length(wheel.body->state().pose.position - Vector3{.x = 10.0F, .y = 5.0F}), 0.0F, 0.01F);
}

TEST_P(JointConformanceTest, JointsBreakUnderAShockWhateverTheSubSteps)
{
    // A crate falls with a lid on a hinge that holds the lid's weight (294 N) but not its stop when the crate lands, within
    // one sub-step: a joint's force is the step's average, so the shock breaks it on any number of sub-steps, at the
    // landing or the step after; a block hanging from a ball-and-socket joint reports its weight on any number of
    // sub-steps.
    for (const std::uint32_t sub_steps : {1U, 4U, 8U})
    {
        const std::unique_ptr<PhysicsWorld> made = world({.solver = {.sub_steps = sub_steps}});
        const Body floor = make_floor(*made);
        const Body crate = make_body(*made, {.pose = {.position = {.y = 4.0F}}}, {.shape = box({.x = 0.5F, .y = 0.4F, .z = 0.5F}), .density = 500.0F});
        const Body lid = make_body(*made, {.pose = {.position = {.y = 4.45F}}}, {.shape = box({.x = 0.5F, .y = 0.05F, .z = 0.5F}), .density = 300.0F});
        const std::unique_ptr<Joint> hinge = made->create_joint(hinge_joint({.first = crate.body->handle(),
                                                                             .second = lid.body->handle(),
                                                                             .anchor = {.y = 4.4F, .z = -0.5F},
                                                                             .axis = {.x = 1.0F},
                                                                             .limits = JointLimits{.lower = -1.2F, .upper = 0.0F},
                                                                             .breaking = {.force = 800.0F}}));
        const Body block = make_body(*made, {.pose = {.position = {.x = 5.0F, .y = 4.0F}}}, {.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
        const std::unique_ptr<Joint> hook = made->create_joint(ball_and_socket_joint({.first = block.body->handle(), .anchor = {.x = 5.0F, .y = 5.0F}}));
        int landed = 0;
        int broke = 0;
        for (int step = 1; step <= 120; ++step)
        {
            made->step(STEP);
            for (const ContactEvent& event : made->events().contacts)
            {
                if (event.kind == ContactEventKind::BEGIN && between(event, crate.collider->handle(), floor.collider->handle()) && landed == 0)
                {
                    landed = step;
                }
            }
            if (std::ranges::any_of(made->events().joints, [&hinge](const JointEvent& event) { return event.joint == hinge->handle(); }))
            {
                broke = step;
            }
        }
        ASSERT_GT(landed, 0) << sub_steps << " sub-steps";
        EXPECT_GE(broke, landed) << sub_steps << " sub-steps: not before the landing";
        EXPECT_LE(broke, landed + 1) << sub_steps << " sub-steps: at the landing";
        EXPECT_TRUE(hinge->state().broken) << sub_steps << " sub-steps";
        EXPECT_NEAR(length(hook->state().force), block.body->mass_properties().mass * STANDARD_GRAVITY, 0.01F * 8.0F * STANDARD_GRAVITY)
            << sub_steps << " sub-steps: the block's weight";
    }
}

TEST_P(JointConformanceTest, SixDegreesOfFreedomJointsKeepTheirLockedAxes)
{
    // Where the backend has them: a box free to slide along x only, thrown in every direction and spun, slides along x and
    // nothing else; a box that may only turn about y, within ±0.3 rad, spun about every axis, stops at its limit in place.
    const std::unique_ptr<PhysicsWorld> made = world({.gravity = {}});
    const Body slider = make_body(*made, {.pose = {.position = {.y = 2.0F}}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    std::array<AxisMotion, 6> motion{};
    motion.fill(AxisMotion::LOCKED);
    motion[std::to_underlying(JointAxis::LINEAR_X)] = AxisMotion::FREE;
    std::unique_ptr<Joint> joint;
    if (!supports(PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS, [&] {
            joint = made->create_joint(six_degrees_of_freedom_joint({.first = slider.body->handle(), .frame = {.position = {.y = 2.0F}}, .motion = motion}));
        }))
    {
        return;
    }
    slider.body->set_linear_velocity({.x = 1.0F, .y = 1.0F, .z = 1.0F});
    slider.body->set_angular_velocity({.x = 0.5F, .y = 2.0F, .z = 0.3F});
    const Body turner = make_body(*made, {.pose = {.position = {.x = 5.0F, .y = 2.0F}}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    std::array<AxisMotion, 6> turning{};
    turning.fill(AxisMotion::LOCKED);
    turning[std::to_underlying(JointAxis::ANGULAR_Y)] = AxisMotion::LIMITED;
    std::array<JointLimits, 6> limits{};
    limits[std::to_underlying(JointAxis::ANGULAR_Y)] = {.lower = -0.3F, .upper = 0.3F};
    const std::unique_ptr<Joint> hinge =
        made->create_joint(six_degrees_of_freedom_joint({.first = turner.body->handle(), .frame = {.position = {.x = 5.0F, .y = 2.0F}}, .motion = turning, .limits = limits}));
    turner.body->set_angular_velocity({.x = 1.0F, .y = 2.0F, .z = 1.0F});
    advance(*made, 60);

    const RigidBodyState sliding = slider.body->state();
    EXPECT_NEAR(sliding.pose.position.x, 1.0F, 0.05F) << "free along x";
    EXPECT_NEAR(sliding.pose.position.y, 2.0F, 0.01F) << "locked along y";
    EXPECT_NEAR(sliding.pose.position.z, 0.0F, 0.01F) << "locked along z";
    EXPECT_GT(std::abs(sliding.pose.orientation.w), 0.999F) << "locked rotations";
    const RigidBodyState turned = turner.body->state();
    EXPECT_LT(length(turned.pose.position - Vector3{.x = 5.0F, .y = 2.0F}), 0.01F);
    EXPECT_NEAR(2.0F * std::atan2(turned.pose.orientation.y, turned.pose.orientation.w), 0.3F, 0.03F) << "at its limit";
    EXPECT_LT(std::abs(turned.pose.orientation.x) + std::abs(turned.pose.orientation.z), 0.01F);
}

/// Keeps the first body's center at a height (one row).
class HeightConstraint final : public UserConstraint
{
public:
    explicit HeightConstraint(float height) noexcept
        : m_height{height}
    {
    }

    [[nodiscard]] auto row_count() const noexcept -> std::uint32_t override
    {
        return 1;
    }

    void rows(const ConstraintFrame& first,
              const ConstraintFrame& /*second*/,
              std::span<ConstraintRow> rows) const noexcept override
    {
        rows[0] = {.kind = ConstraintRowKind::EQUALITY, .linear_first = {.y = 1.0F}, .error = first.center.y - m_height};
    }

private:
    float m_height;
};

TEST_P(JointConformanceTest, UserConstraintsHoldTheirBodiesAndBreak)
{
    // Where the backend has them: a ball held at 2 m by its constraint moves freely sideways; a heavy one breaks a weak
    // constraint and falls.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body held = make_body(*made, {.pose = {.position = {.y = 2.0F}}, .linear_velocity = {.x = 1.0F}}, {.shape = sphere(0.1F)});
    std::unique_ptr<Constraint> height;
    if (!supports(PhysicsCapability::USER_CONSTRAINTS,
                  [&] { height = made->create_constraint({.first = held.body->handle(), .constraint = std::make_shared<HeightConstraint>(2.0F)}); }))
    {
        return;
    }
    const Body heavy = make_body(*made, {.pose = {.position = {.x = 3.0F, .y = 2.0F}}}, {.shape = sphere(0.3F), .density = 5000.0F});
    const std::unique_ptr<Constraint> weak =
        made->create_constraint({.first = heavy.body->handle(), .constraint = std::make_shared<HeightConstraint>(2.0F), .breaking = {.force = 100.0F}});
    bool broke = false;
    for (int step = 0; step < 60; ++step)
    {
        made->step(STEP);
        broke = broke || std::ranges::any_of(made->events().joints, [&weak](const JointEvent& event) { return event.constraint == weak->handle(); });
    }
    const float weight = held.body->mass_properties().mass * STANDARD_GRAVITY;
    EXPECT_NEAR(held.body->state().pose.position.y, 2.0F, 0.01F) << "held at its height";
    EXPECT_NEAR(held.body->state().pose.position.x, 1.0F, 0.05F) << "free sideways";
    EXPECT_NEAR(length(height->state().force), weight, 0.05F * weight) << "it carries the ball's weight";
    EXPECT_TRUE(broke);
    EXPECT_TRUE(weak->state().broken);
    EXPECT_LT(heavy.body->state().pose.position.y, 0.0F) << "fallen once free";
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         JointConformanceTest,
                         testing::ValuesIn(conformance_backends()),
                         conformance_test_name);

} // namespace
} // namespace gfx::tests::conformance
