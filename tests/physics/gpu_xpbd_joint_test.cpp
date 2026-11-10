// The GPU backend's joints (phase G6), on each GPU of the machine with the Vulkan validation layers, in both solver modes
// (graph coloring, Jacobi): the reference backend's joint scenarios (a pendulum's period, a hinge's limit, a rope's
// length, a weld breaking, a motor's speed, a hinge breaking under a shock whatever the substeps while a hook reports its
// load's weight, a six-degrees-of-freedom joint's locked and limited axes), then what only these tests check: sliders
// within their limits under their motors, cone-twist joints' swing and twist limits, springs at their frequencies (a
// distance spring, a hinge's spring, a soft weld's sag), rods and stubs, joined bodies not colliding unless asked, a
// chain sleeping and waking as one island, and the same steps giving the same bits.
#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;
using gfx::Quaternion;
using gfx::Transform;
using gfx::Vector3;

constexpr float STEP = 1.0F / 60.0F;
constexpr float PI = std::numbers::pi_v<float>;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    return gfx::tests::usable_physical_devices();
}

auto
box_shape(Vector3 half) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half});
}

auto
sphere_shape(float radius) -> std::shared_ptr<const Shape>
{
    return make_shape(SphereShape{.radius = radius});
}

/// A world with its bodies, colliders and joints (the joints destroyed first, then the colliders, then the bodies, then
/// the world).
struct Scene
{
    std::unique_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<RigidBody>> bodies{};
    std::vector<std::unique_ptr<Collider>> colliders{};
    std::vector<std::unique_ptr<Joint>> joints{};

    Scene(std::unique_ptr<PhysicsWorld> made)
        : world{std::move(made)}
    {
    }

    Scene(Scene&&) = default;

    ~Scene()
    {
        joints.clear();
        colliders.clear();
        bodies.clear();
    }

    auto add(const RigidBodyDescription& body,
             const ColliderDescription& collider) -> RigidBody&
    {
        bodies.push_back(world->create_rigid_body(body));
        colliders.push_back(bodies.back()->add_collider(collider));
        return *bodies.back();
    }

    auto join(const JointDescription& description) -> Joint&
    {
        joints.push_back(world->create_joint(description));
        return *joints.back();
    }

    /// A static floor, its top at y = 0.
    auto floor() -> RigidBody&
    {
        return add({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, {.shape = box_shape({.x = 30, .y = 0.5F, .z = 30})});
    }

    void run(float seconds)
    {
        const auto steps = static_cast<int>(std::lround(seconds / STEP));
        for (int step = 0; step < steps; ++step)
        {
            world->step(STEP);
        }
    }
};

auto
finite(const RigidBodyState& state) -> bool
{
    const auto vector_finite = [](Vector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
    return vector_finite(state.pose.position) && vector_finite(state.linear_velocity) && vector_finite(state.angular_velocity);
}

/// The angle of @p orientation about the world axis @p axis (0 x, 1 y, 2 z), as 2 atan2 of its component and w.
auto
angle_about(const Quaternion& orientation,
            std::size_t axis) -> float
{
    const std::array<float, 3> components{orientation.x, orientation.y, orientation.z};
    return 2.0F * std::atan2(components[axis], orientation.w);
}

/// The mean period of a signal sampled every step, from the times it crosses @p level going down (interpolated).
auto
period_of(const std::vector<float>& samples,
          float level) -> float
{
    std::vector<float> crossings;
    for (std::size_t index = 1; index < samples.size(); ++index)
    {
        const float previous = samples[index - 1] - level;
        const float current = samples[index] - level;
        if (previous > 0.0F && current <= 0.0F)
        {
            crossings.push_back((static_cast<float>(index) - current / (current - previous)) * STEP);
        }
    }
    if (crossings.size() < 2)
    {
        return 0.0F;
    }
    return (crossings.back() - crossings.front()) / static_cast<float>(crossings.size() - 1);
}

/// The bytes of the states of @p bodies.
auto
state_bytes(const std::vector<RigidBody*>& bodies) -> std::vector<std::byte>
{
    std::vector<std::byte> bytes;
    for (const RigidBody* body : bodies)
    {
        const RigidBodyState state = body->state();
        const auto append = [&bytes](const auto& value) {
            const auto view = std::as_bytes(std::span{&value, 1});
            bytes.insert(bytes.end(), view.begin(), view.end());
        };
        append(state.pose.position);
        append(state.pose.orientation);
        append(state.linear_velocity);
        append(state.angular_velocity);
    }
    return bytes;
}

// ===================================================================================================== fixture

/// A world on each GPU in each solver mode, with the validation layers.
class GpuXpbdJointTest : public gfx::tests::VulkanDeviceTest,
                         public testing::WithParamInterface<std::tuple<std::size_t, GpuSolverMode>>
{
protected:
    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(std::get<0>(GetParam()));
    }

    [[nodiscard]] auto required_features() const -> std::vector<gfx::DeviceFeature> override
    {
        if (m_physical_device->supports(gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS))
        {
            return {gfx::DeviceFeature::SHADER_INTEGER64_ATOMICS};
        }
        return {};
    }

    [[nodiscard]] auto mode() const -> GpuSolverMode
    {
        return std::get<1>(GetParam());
    }

    [[nodiscard]] auto scene(const PhysicsWorldDescription& description = {},
                             GpuOptimization optimizations = DEFAULT_GPU_OPTIMIZATIONS) -> Scene
    {
        return Scene{gpu::create_physics_world(description, *m_device, {.solver_mode = mode(), .optimizations = optimizations})};
    }
};

// ===================================================================================================== the reference's scenarios

TEST_P(GpuXpbdJointTest, APendulumSwingsWithItsPeriod)
{
    // A small heavy bob 1 m below a hinge to the world, released at 5 degrees: T = 2π √(L / g).
    Scene world = scene();
    constexpr float LENGTH = 1.0F;
    constexpr float START = 5.0F * PI / 180.0F;
    const Vector3 bob_position{.x = LENGTH * std::sin(START), .y = 2.0F - LENGTH * std::cos(START)};
    RigidBody& bob = world.add({.pose = {.position = bob_position}, .angular_damping = 0.0F}, {.shape = sphere_shape(0.02F), .density = 20000.0F});
    (void)world.join(hinge_joint({.first = bob.handle(), .anchor = {.y = 2.0F}, .axis = {.z = 1.0F}}));
    std::vector<float> xs{bob_position.x};
    for (int step = 0; step < 60 * 7; ++step)
    {
        world.world->step(STEP);
        xs.push_back(bob.state().pose.position.x);
    }
    const float expected = 2.0F * PI * std::sqrt(LENGTH / STANDARD_GRAVITY);
    EXPECT_NEAR(period_of(xs, 0.0F), expected, 0.02F * expected);
    EXPECT_NEAR(gfx::length(bob.state().pose.position - Vector3{.y = 2.0F}), LENGTH, 0.01F) << "the hinge holds the distance";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, JointsHoldTheirLimitsRopesTheirLengthAndWeldsBreak)
{
    Scene world = scene();

    // An arm on a hinge limited to ±0.5 rad from horizontal falls to its lower limit.
    RigidBody& arm = world.add({.pose = {.position = {.x = 1.0F, .y = 5.0F}}}, {.shape = box_shape({.x = 1.0F, .y = 0.05F, .z = 0.05F})});
    (void)world.join(hinge_joint({.first = arm.handle(), .anchor = {.y = 5.0F}, .axis = {.z = 1.0F}, .limits = JointLimits{.lower = -0.5F, .upper = 0.5F}}));

    // A ball on a rope of 2 m from a point 10 m up falls to the rope's end.
    RigidBody& ball = world.add({.pose = {.position = {.x = 5.0F, .y = 9.5F}}}, {.shape = sphere_shape(0.1F)});
    (void)world.join(distance_joint({.first = ball.handle(), .anchor_first = {}, .anchor_second = {.x = 5.0F, .y = 10.0F}, .length = 2.0F, .mode = DistanceMode::ROPE}));

    // A heavy block welded to the world with a weak weld breaks.
    RigidBody& block = world.add({.pose = {.position = {.x = -5.0F, .y = 5.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .density = 5000.0F});
    Joint& weld = world.join(fixed_joint({.first = block.handle(), .anchor = {.x = -5.0F, .y = 5.0F}, .breaking = {.force = 1000.0F}}));

    // A wheel on a hinge to the world with a motor spins up to its target speed (the world relative to the wheel: the
    // wheel turns at minus it).
    RigidBody& wheel = world.add({.pose = {.position = {.x = 10.0F, .y = 5.0F}}, .angular_damping = 0.0F}, {.shape = sphere_shape(0.5F)});
    (void)world.join(
        hinge_joint({.first = wheel.handle(), .anchor = {.x = 10.0F, .y = 5.0F}, .axis = {.z = 1.0F}, .motor = JointMotor{.target_speed = 3.0F, .maximum_force = 500.0F}}));

    bool broke = false;
    for (int step = 0; step < 180; ++step)
    {
        world.world->step(STEP);
        broke = broke || std::ranges::any_of(world.world->events().joints, [&weld](const JointEvent& event) { return event.joint == weld.handle(); });
    }
    const Vector3 tip = gfx::transform_point(arm.state().pose, Vector3{.x = 1.0F});
    EXPECT_NEAR(std::atan2(tip.y - 5.0F, tip.x), -0.5F, 0.05F) << "the lower limit";
    EXPECT_LE(gfx::length(ball.state().pose.position - Vector3{.x = 5.0F, .y = 10.0F}), 2.0F + 0.02F);
    EXPECT_GT(gfx::length(ball.state().pose.position - Vector3{.x = 5.0F, .y = 10.0F}), 1.9F);
    EXPECT_TRUE(broke);
    EXPECT_TRUE(weld.state().broken);
    EXPECT_FALSE(weld.state().enabled);
    EXPECT_LT(block.state().pose.position.y, 0.0F) << "it fell once free";
    EXPECT_NEAR(wheel.state().angular_velocity.z, -3.0F, 0.05F) << "the motor's speed";
    EXPECT_NEAR(gfx::length(wheel.state().pose.position - Vector3{.x = 10.0F, .y = 5.0F}), 0.0F, 0.01F);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, JointsBreakUnderAShockWhateverTheSubSteps)
{
    // A crate falls with a lid on a hinge that holds the lid's weight (294 N) but not its stop when the crate lands, which
    // happens within one substep: the force is the step's average (the impulse over the step, per second), so the shock
    // counts on any number of substeps. A steady load gives the same force on any number of substeps: a block's weight.
    // (The landing is the step the crate's fall stops: contact events arrive with phase G8.)
    for (const std::uint32_t sub_steps : {1U, 4U, 8U})
    {
        Scene world = scene({.solver = {.sub_steps = sub_steps}});
        world.floor();
        RigidBody& crate = world.add({.pose = {.position = {.y = 4.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.4F, .z = 0.5F}), .density = 500.0F});
        RigidBody& lid = world.add({.pose = {.position = {.y = 4.45F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.05F, .z = 0.5F}), .density = 300.0F});
        Joint& hinge = world.join(hinge_joint({.first = crate.handle(),
                                               .second = lid.handle(),
                                               .anchor = {.y = 4.4F, .z = -0.5F},
                                               .axis = {.x = 1.0F},
                                               .limits = JointLimits{.lower = -1.2F, .upper = 0.0F},
                                               .breaking = {.force = 800.0F}}));
        RigidBody& block = world.add({.pose = {.position = {.x = 5.0F, .y = 4.0F}}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
        Joint& hook = world.join(ball_and_socket_joint({.first = block.handle(), .anchor = {.x = 5.0F, .y = 5.0F}}));

        int landed = 0;
        int broke = 0;
        float previous_speed = 0.0F;
        for (int step = 1; step <= 120; ++step)
        {
            world.world->step(STEP);
            const float speed = crate.state().linear_velocity.y;
            if (landed == 0 && previous_speed < -3.0F && speed > previous_speed + 3.0F)
            {
                landed = step;
            }
            previous_speed = speed;
            if (std::ranges::any_of(world.world->events().joints, [&hinge](const JointEvent& event) { return event.joint == hinge.handle(); }))
            {
                broke = step;
            }
        }
        ASSERT_GT(landed, 0) << sub_steps << " substeps";
        EXPECT_GE(broke, landed) << sub_steps << " substeps: not before the landing";
        EXPECT_LE(broke, landed + 1) << sub_steps << " substeps: at the landing";
        EXPECT_TRUE(hinge.state().broken) << sub_steps << " substeps";
        EXPECT_NEAR(gfx::length(hook.state().force), block.mass_properties().mass * STANDARD_GRAVITY, 0.01F * 8.0F * STANDARD_GRAVITY)
            << sub_steps << " substeps: the block's weight";
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, SixDegreesOfFreedomJointsKeepTheirLockedAxes)
{
    // A box free to slide along x only, thrown in every direction and spun: it slides along x, nothing else.
    Scene world = scene({.gravity = {}});
    RigidBody& slider = world.add({.pose = {.position = {.y = 2.0F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    std::array<AxisMotion, 6> motion{};
    motion.fill(AxisMotion::LOCKED);
    motion[std::to_underlying(JointAxis::LINEAR_X)] = AxisMotion::FREE;
    (void)world.join(six_degrees_of_freedom_joint({.first = slider.handle(), .frame = {.position = {.y = 2.0F}}, .motion = motion}));
    slider.set_linear_velocity({.x = 1.0F, .y = 1.0F, .z = 1.0F});
    slider.set_angular_velocity({.x = 0.5F, .y = 2.0F, .z = 0.3F});

    // A box that may only turn about y, within ±0.3 rad, spun about every axis: it stops at its limit, in place.
    RigidBody& turner = world.add({.pose = {.position = {.x = 5.0F, .y = 2.0F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    std::array<AxisMotion, 6> turning{};
    turning.fill(AxisMotion::LOCKED);
    turning[std::to_underlying(JointAxis::ANGULAR_Y)] = AxisMotion::LIMITED;
    std::array<JointLimits, 6> limits{};
    limits[std::to_underlying(JointAxis::ANGULAR_Y)] = {.lower = -0.3F, .upper = 0.3F};
    (void)world.join(six_degrees_of_freedom_joint({.first = turner.handle(), .frame = {.position = {.x = 5.0F, .y = 2.0F}}, .motion = turning, .limits = limits}));
    turner.set_angular_velocity({.x = 1.0F, .y = 2.0F, .z = 1.0F});
    world.run(1.0F);

    const RigidBodyState sliding = slider.state();
    EXPECT_NEAR(sliding.pose.position.x, 1.0F, 0.05F) << "free along x";
    EXPECT_NEAR(sliding.pose.position.y, 2.0F, 0.01F) << "locked along y";
    EXPECT_NEAR(sliding.pose.position.z, 0.0F, 0.01F) << "locked along z";
    EXPECT_GT(std::abs(sliding.pose.orientation.w), 0.999F) << "locked rotations";
    const RigidBodyState turned = turner.state();
    EXPECT_LT(gfx::length(turned.pose.position - Vector3{.x = 5.0F, .y = 2.0F}), 0.01F);
    EXPECT_NEAR(angle_about(turned.pose.orientation, 1), 0.3F, 0.03F) << "at its limit";
    EXPECT_LT(std::abs(turned.pose.orientation.x) + std::abs(turned.pose.orientation.z), 0.01F);
    EXPECT_TRUE(take_validation_messages().empty());
}

// ===================================================================================================== further kinds

TEST_P(GpuXpbdJointTest, SlidersStayOnTheirAxisWithinTheirLimitsUnderTheirMotors)
{
    // A block of 64 kg on a horizontal slider to the world, its motor of 1000 N driving it (the world relative to the block
    // at -2 m/s: the block at +2 m/s, reached at 15.6 m/s² in 0.128 s) into the limit 0.5 m away; gravity pulls it across
    // the axis, which holds.
    Scene world = scene();
    RigidBody& block = world.add({.pose = {.position = {.y = 3.0F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    (void)world.join(slider_joint({.first = block.handle(),
                                   .anchor = {.y = 3.0F},
                                   .axis = {.x = 1.0F},
                                   .limits = JointLimits{.lower = -0.5F, .upper = 0.5F},
                                   .motor = JointMotor{.target_speed = -2.0F, .maximum_force = 1000.0F}}));
    world.run(0.05F);
    EXPECT_NEAR(block.state().linear_velocity.x, 1000.0F / 64.0F * 0.05F, 0.02F) << "the motor's force";
    world.run(0.2F);
    EXPECT_NEAR(block.state().linear_velocity.x, 2.0F, 0.02F) << "the motor's speed";
    world.run(0.75F);
    const RigidBodyState state = block.state();
    EXPECT_NEAR(state.pose.position.x, 0.5F, 0.02F) << "the limit";
    EXPECT_NEAR(state.pose.position.y, 3.0F, 0.01F) << "on the axis";
    EXPECT_NEAR(state.pose.position.z, 0.0F, 0.01F);
    EXPECT_GT(std::abs(state.pose.orientation.w), 0.999F) << "not turned";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, ConeTwistJointsKeepTheirSwingAndTwistLimits)
{
    // Two blocks hanging 1 m below cone-twist joints to the world (axis up): one pushed sideways (it would swing 1 rad),
    // the other spun about the vertical (twist limits ±0.2 rad). Neither passes its limit.
    Scene world = scene({.sleep = {.enabled = false}});
    RigidBody& swinging = world.add({.pose = {.position = {.y = 2.0F}}, .linear_velocity = {.x = 3.0F}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
    (void)world.join(cone_twist_joint({.first = swinging.handle(), .anchor = {.y = 3.0F}, .axis = {.y = 1.0F}, .swing_limit = 0.3F}));
    RigidBody& twisting = world.add({.pose = {.position = {.x = 3.0F, .y = 2.0F}}, .angular_velocity = {.y = 5.0F}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
    (void)world.join(cone_twist_joint({.first = twisting.handle(),
                                       .anchor = {.x = 3.0F, .y = 3.0F},
                                       .axis = {.y = 1.0F},
                                       .swing_limit = 0.3F,
                                       .twist_limits = JointLimits{.lower = -0.2F, .upper = 0.2F}}));
    float largest_swing = 0.0F;
    float largest_twist = 0.0F;
    for (int step = 0; step < 120; ++step)
    {
        world.world->step(STEP);
        const Vector3 hanging = gfx::normalized(swinging.state().pose.position - Vector3{.y = 3.0F});
        largest_swing = std::max(largest_swing, std::acos(std::clamp(-hanging.y, -1.0F, 1.0F)));
        largest_twist = std::max(largest_twist, std::abs(angle_about(twisting.state().pose.orientation, 1)));
    }
    EXPECT_LT(largest_swing, 0.3F + 0.03F);
    EXPECT_GT(largest_swing, 0.25F) << "it swung to the cone";
    EXPECT_LT(largest_twist, 0.2F + 0.03F);
    EXPECT_GT(largest_twist, 0.15F) << "it turned to the limit";
    EXPECT_NEAR(gfx::length(swinging.state().pose.position - Vector3{.y = 3.0F}), 1.0F, 0.01F);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, SpringsOscillateAtTheirFrequencies)
{
    // Without gravity: a ball on a distance spring of 1 Hz stretched 0.5 m, a box on a hinge spring of 1 Hz set turning;
    // their periods are 1 s. With gravity: a block on a soft weld of 2 Hz sags g / ω² (critically damped: it settles).
    Scene free = scene({.gravity = {}, .sleep = {.enabled = false}});
    RigidBody& ball = free.add({.pose = {.position = {.x = 1.5F}}, .angular_damping = 0.0F}, {.shape = sphere_shape(0.1F)});
    (void)free.join(distance_joint({.first = ball.handle(),
                                    .anchor_first = {},
                                    .anchor_second = {},
                                    .length = 1.0F,
                                    .mode = DistanceMode::SPRING,
                                    .spring = {.frequency = 1.0F, .damping_ratio = 0.0F}}));
    RigidBody& wheel = free.add({.pose = {.position = {.x = -3.0F}}, .angular_velocity = {.z = 1.0F}, .angular_damping = 0.0F},
                                {.shape = box_shape({.x = 0.3F, .y = 0.1F, .z = 0.1F})});
    (void)free.join(hinge_joint({.first = wheel.handle(), .anchor = {.x = -3.0F}, .axis = {.z = 1.0F}, .spring = {.frequency = 1.0F, .damping_ratio = 0.0F}}));
    std::vector<float> stretches{0.5F};
    std::vector<float> angles{0.0F};
    for (int step = 0; step < 60 * 4; ++step)
    {
        free.world->step(STEP);
        stretches.push_back(gfx::length(ball.state().pose.position) - 1.0F);
        angles.push_back(angle_about(wheel.state().pose.orientation, 2));
    }
    EXPECT_NEAR(period_of(stretches, 0.0F), 1.0F, 0.03F) << "the distance spring";
    EXPECT_NEAR(period_of(angles, 0.0F), 1.0F, 0.03F) << "the hinge spring";
    EXPECT_NEAR(*std::ranges::max_element(angles), 1.0F / (2.0F * PI), 0.01F) << "the hinge spring's amplitude: ω₀ / ω";

    Scene heavy = scene({.sleep = {.enabled = false}});
    RigidBody& block = heavy.add({.pose = {.position = {.y = 5.0F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    (void)heavy.join(fixed_joint({.first = block.handle(), .anchor = {.y = 5.0F}, .linear_spring = {.frequency = 2.0F, .damping_ratio = 1.0F}}));
    heavy.run(3.0F);
    const float omega = 2.0F * PI * 2.0F;
    EXPECT_NEAR(5.0F - block.state().pose.position.y, STANDARD_GRAVITY / (omega * omega), 0.1F * STANDARD_GRAVITY / (omega * omega)) << "the weld's sag";
    EXPECT_LT(gfx::length(block.state().linear_velocity), 0.01F) << "settled";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, RodsAndStubsHoldTheirDistances)
{
    // A ball swinging on a rod of 1.5 m from the world, released sideways; a box hanging from a stub at its corner.
    Scene world = scene({.sleep = {.enabled = false}});
    RigidBody& ball = world.add({.pose = {.position = {.x = 1.5F, .y = 5.0F}}}, {.shape = sphere_shape(0.1F)});
    (void)world.join(distance_joint({.first = ball.handle(), .anchor_first = {}, .anchor_second = {.y = 5.0F}, .length = 1.5F, .mode = DistanceMode::ROD}));
    RigidBody& box = world.add({.pose = {.position = {.x = 5.0F, .y = 5.0F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    (void)world.join(distance_joint(
        {.first = box.handle(), .anchor_first = {.x = 0.2F, .y = 0.2F, .z = 0.2F}, .anchor_second = {.x = 5.2F, .y = 5.2F, .z = 0.2F}, .length = 0.0F, .mode = DistanceMode::STUB}));
    float rod_error = 0.0F;
    float stub_error = 0.0F;
    for (int step = 0; step < 120; ++step)
    {
        world.world->step(STEP);
        rod_error = std::max(rod_error, std::abs(gfx::length(ball.state().pose.position - Vector3{.y = 5.0F}) - 1.5F));
        stub_error = std::max(stub_error, gfx::length(gfx::transform_point(box.state().pose, Vector3{.x = 0.2F, .y = 0.2F, .z = 0.2F}) - Vector3{.x = 5.2F, .y = 5.2F, .z = 0.2F}));
    }
    EXPECT_LT(rod_error, 0.01F);
    EXPECT_LT(stub_error, 0.01F);
    EXPECT_LT(ball.state().pose.position.y, 4.0F) << "it swung down";
    EXPECT_LT(box.state().pose.position.y, 5.0F) << "it hangs from its corner";
    EXPECT_TRUE(take_validation_messages().empty());
}

// ===================================================================================================== joints and the rest

TEST_P(GpuXpbdJointTest, JoinedBodiesCollideOnlyWhenAsked)
{
    // Two overlapping boxes joined at their middle, without gravity: kept apart by the joint they never touch; joined with
    // collide_connected their contacts push on them.
    for (const bool collide : {false, true})
    {
        Scene world = scene({.gravity = {}});
        RigidBody& first = world.add({.pose = {.position = {.y = 1.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
        RigidBody& second = world.add({.pose = {.position = {.x = 0.5F, .y = 1.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
        (void)world.join(ball_and_socket_joint({.first = first.handle(), .second = second.handle(), .anchor = {.x = 0.25F, .y = 1.0F}, .collide_connected = collide}));
        world.world->step(STEP);
        if (collide)
        {
            EXPECT_GT(world.world->statistics().contacts, 0U) << "they touch";
            continue;
        }
        EXPECT_EQ(world.world->statistics().contacts, 0U) << "kept apart";
        world.run(0.5F);
        EXPECT_LT(gfx::length(first.state().pose.position - Vector3{.y = 1.0F}), 1e-4F);
        EXPECT_LT(gfx::length(second.state().pose.position - Vector3{.x = 0.5F, .y = 1.0F}), 1e-4F);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, AChainSleepsAndWakesAsOneIsland)
{
    // Four boxes hanging in a chain of ball joints from the world fall asleep together; a push on the lowest wakes the
    // highest through the joints.
    Scene world = scene();
    std::vector<RigidBody*> links;
    for (int index = 0; index < 4; ++index)
    {
        const float y = 4.8F - 0.4F * static_cast<float>(index);
        links.push_back(&world.add({.pose = {.position = {.y = y}}}, {.shape = box_shape({.x = 0.1F, .y = 0.2F, .z = 0.1F})}));
        const RigidBodyHandle above = index == 0 ? RigidBodyHandle{} : links[static_cast<std::size_t>(index - 1)]->handle();
        (void)world.join(ball_and_socket_joint({.first = links.back()->handle(), .second = above, .anchor = {.y = y + 0.2F}}));
    }
    world.run(3.0F);
    for (const RigidBody* link : links)
    {
        EXPECT_FALSE(link->state().awake);
        EXPECT_NEAR(link->state().pose.position.x, 0.0F, 0.01F);
    }
    links.back()->apply_linear_impulse({.x = 0.5F});
    world.world->step(STEP);
    EXPECT_TRUE(links.front()->state().awake) << "woken through the chain";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, TheSameStepsGiveTheSameBits)
{
    // Two worlds of the same jointed scene (a chain of hinges with a motor falling onto a floor, a rope, a slider): their
    // states equal bit for bit every 30 steps of 3 s.
    const auto build = [](Scene& world) {
        world.floor();
        std::vector<RigidBody*> bodies;
        for (int index = 0; index < 6; ++index)
        {
            const float x = 0.5F * static_cast<float>(index);
            bodies.push_back(&world.add({.pose = {.position = {.x = x, .y = 3.0F}}}, {.shape = box_shape({.x = 0.25F, .y = 0.1F, .z = 0.2F})}));
            if (index > 0)
            {
                const JointMotor motor{.target_speed = 1.0F, .maximum_force = 5.0F};
                (void)world.join(hinge_joint({.first = bodies[static_cast<std::size_t>(index - 1)]->handle(),
                                              .second = bodies.back()->handle(),
                                              .anchor = {.x = x - 0.25F, .y = 3.0F},
                                              .axis = {.z = 1.0F},
                                              .limits = JointLimits{.lower = -1.0F, .upper = 1.0F},
                                              .motor = index == 3 ? std::optional{motor} : std::nullopt}));
            }
        }
        bodies.push_back(&world.add({.pose = {.position = {.x = -3.0F, .y = 2.0F}}}, {.shape = sphere_shape(0.2F)}));
        (void)world.join(distance_joint({.first = bodies.back()->handle(), .anchor_second = {.x = -2.0F, .y = 3.0F}, .length = 1.2F, .mode = DistanceMode::ROPE}));
        bodies.push_back(&world.add({.pose = {.position = {.x = 5.0F, .y = 2.0F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})}));
        (void)world.join(slider_joint({.first = bodies.back()->handle(), .anchor = {.x = 5.0F, .y = 2.0F}, .axis = gfx::normalized(Vector3{.x = 1.0F, .y = -1.0F})}));
        return bodies;
    };
    Scene first = scene();
    const std::vector<RigidBody*> first_bodies = build(first);
    Scene second = scene();
    const std::vector<RigidBody*> second_bodies = build(second);
    for (int step = 0; step < 180; ++step)
    {
        first.world->step(STEP);
        second.world->step(STEP);
        if (step % 30 == 29)
        {
            ASSERT_EQ(state_bytes(first_bodies), state_bytes(second_bodies)) << "step " << step;
        }
    }
    for (const RigidBody* body : first_bodies)
    {
        EXPECT_TRUE(finite(body->state()));
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, TheOptimizationsGiveTheBaselinesBits)
{
    // The default optimizations against none (the baseline) on a scene whose layouts change while it runs — a joint
    // disabled, a collider's material changed, a body and a collider made, a joint breaking during a step — between steps
    // whose edits keep them (forces, impulses, velocities): the same states bit for bit every 15 steps, the same events.
    const auto build = [](Scene& world) {
        world.floor();
        std::vector<RigidBody*> bodies;
        for (int index = 0; index < 5; ++index)
        {
            const float x = 0.5F * static_cast<float>(index);
            bodies.push_back(&world.add({.pose = {.position = {.x = x, .y = 2.0F}}}, {.shape = box_shape({.x = 0.25F, .y = 0.1F, .z = 0.2F})}));
            if (index > 0)
            {
                (void)world.join(hinge_joint({.first = bodies[static_cast<std::size_t>(index - 1)]->handle(),
                                              .second = bodies.back()->handle(),
                                              .anchor = {.x = x - 0.25F, .y = 2.0F},
                                              .axis = {.z = 1.0F}}));
            }
        }
        bodies.push_back(&world.add({.pose = {.position = {.x = -3.0F, .y = 3.0F}}}, {.shape = box_shape({.x = 0.3F, .y = 0.3F, .z = 0.3F})}));
        (void)world.join(fixed_joint({.first = bodies.back()->handle(), .anchor = {.x = -3.0F, .y = 3.3F}, .breaking = {.force = 60.0F}}));
        for (int index = 0; index < 6; ++index)
        {
            bodies.push_back(&world.add({.pose = {.position = {.x = 3.0F + 0.45F * static_cast<float>(index), .y = 0.2F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})}));
        }
        return bodies;
    };
    const auto edit = [](Scene& world, std::vector<RigidBody*>& bodies, int step) {
        // Edits that keep the layouts, every step; one that changes them now and then (the second floor box slides at
        // 1 m/s every 7 steps: how far depends on its friction, made 0.05 at step 55).
        bodies[0]->apply_force({.y = 3.0F});
        if (step % 7 == 0)
        {
            bodies[7]->set_linear_velocity({.x = 1.0F});
            bodies[8]->apply_linear_impulse({.z = 20.0F});
        }
        switch (step)
        {
            case 40: world.joints[1]->set_enabled(false); break;
            case 55: world.colliders[8]->set_material({.friction = {.static_coefficient = 0.05F, .dynamic_coefficient = 0.05F}}); break;
            case 70: bodies.push_back(&world.add({.pose = {.position = {.x = 3.5F, .y = 1.5F}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})})); break;
            case 85: bodies[5]->set_linear_velocity({.y = -20.0F}); break;
            default: break;
        }
    };
    Scene optimized = scene();
    std::vector<RigidBody*> optimized_bodies = build(optimized);
    Scene baseline = scene({}, GpuOptimization::NONE);
    std::vector<RigidBody*> baseline_bodies = build(baseline);
    std::size_t broken = 0;
    for (int step = 0; step < 150; ++step)
    {
        edit(optimized, optimized_bodies, step);
        edit(baseline, baseline_bodies, step);
        optimized.world->step(STEP);
        baseline.world->step(STEP);
        const PhysicsEvents optimized_events = optimized.world->events();
        const PhysicsEvents baseline_events = baseline.world->events();
        ASSERT_EQ(optimized_events.contacts.size(), baseline_events.contacts.size()) << "step " << step;
        ASSERT_EQ(optimized_events.joints.size(), baseline_events.joints.size()) << "step " << step;
        broken += optimized_events.joints.size();
        if (step % 15 == 14)
        {
            ASSERT_EQ(state_bytes(optimized_bodies), state_bytes(baseline_bodies)) << "step " << step;
        }
    }
    EXPECT_EQ(broken, 1U) << "the weld broke under the box's weight";
    EXPECT_TRUE(take_validation_messages().empty());
}

auto
joint_test_name(const testing::TestParamInfo<std::tuple<std::size_t, GpuSolverMode>>& parameter) -> std::string
{
    return std::format("Device{}_{}", std::get<0>(parameter.param), std::get<1>(parameter.param));
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdJointTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         joint_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdJointTest);

// ===================================================================================================== measurements

/// The step time of many chains on each GPU (printed).
class GpuXpbdJointMeasurementTest : public GpuXpbdJointTest
{
};

TEST_P(GpuXpbdJointMeasurementTest, StepTimeIsMeasured)
{
    // 50 chains of 20 boxes on ball joints (1000 joints) swinging from the world, sleep off: the mean step time over 60
    // steps after 30.
    Scene world = scene({.sleep = {.enabled = false}});
    constexpr int CHAINS = 50;
    constexpr int LINKS = 20;
    for (int chain = 0; chain < CHAINS; ++chain)
    {
        RigidBody* above = nullptr;
        for (int link = 0; link < LINKS; ++link)
        {
            const Vector3 position{.x = static_cast<float>(chain % 10) * 1.5F + 0.2F * static_cast<float>(link), .y = 30.0F - 0.5F * static_cast<float>(link),
                                   .z = static_cast<float>(chain / 10) * 1.5F};
            RigidBody& body = world.add({.pose = {.position = position}}, {.shape = box_shape({.x = 0.1F, .y = 0.2F, .z = 0.1F})});
            (void)world.join(ball_and_socket_joint({.first = body.handle(), .second = above != nullptr ? above->handle() : RigidBodyHandle{}, .anchor = position + Vector3{.y = 0.25F}}));
            above = &body;
        }
    }
    world.run(0.5F);
    const auto start = std::chrono::steady_clock::now();
    world.run(1.0F);
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60.0;
    std::cout << std::format("{} | {} | 1000 ball joints in 50 chains: {:.2f} ms per step\n", m_physical_device->properties().name, mode(), milliseconds);
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdJointMeasurementTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         joint_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdJointMeasurementTest);

} // namespace
