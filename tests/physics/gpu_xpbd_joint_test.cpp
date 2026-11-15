// The GPU backend's joints (phase G6), on each GPU of the machine with the Vulkan validation layers, in both solver modes
// (graph coloring, Jacobi): the reference backend's joint scenarios (a pendulum's period, a hinge's limit, a rope's
// length, a weld breaking, a motor's speed, a hinge breaking under a shock whatever the substeps while a hook reports its
// load's weight, a six-degrees-of-freedom joint's locked and limited axes), then what only these tests check: sliders
// within their limits under their motors, cone-twist joints' swing and twist limits, springs at their frequencies (a
// distance spring, a hinge's spring, a soft weld's sag), rods and stubs, joined bodies not colliding unless asked, a
// chain sleeping and waking as one island, chains of links swinging at their speed in either mode (joints between rigid
// bodies split equally in the overflow set), the constraints of new colors joining the overflow set (OVERFLOW_NEW_COLORS),
// and the same steps giving the same bits.
#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;
import gfx.profiler;

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

TEST_P(GpuXpbdJointTest, ChainsOfLinksSwingAtTheirSpeedInEitherMode)
{
    // Eight chains of 24 capsule links on ball joints, released level side by side (their links do not collide): each
    // link is held at both ends by joints of equal masses, which the overflow set's shares did not split (a share of one
    // half each), so in the Jacobi mode both joints turned the link by their whole error: the chains reached 677 m/s
    // and then NaN within 0.2 s on one GPU, 59 m/s on the other. Split equally (a whole share per joint between rigid
    // bodies), they swing as in the coloring mode: their tips whip past the speed of a fall from the chain's length (25 m/s
    // against 13 in both modes) but stay under three times it, and their joints hold (each link's end within 3 cm of the
    // next one's: 1.4 cm in the Jacobi mode, which converges slower).
    constexpr int CHAINS = 8;
    constexpr int LINKS = 24;
    constexpr float PITCH = 0.36F;
    Scene world = scene();
    std::vector<RigidBody*> links;
    const std::shared_ptr<const Shape> link = make_shape(CapsuleShape{.half_height = 0.15F, .radius = 0.06F});
    const Quaternion along_x = Quaternion::from_axis_angle({.z = 1.0F}, 0.5F * PI);
    for (int chain = 0; chain < CHAINS; ++chain)
    {
        const Vector3 anchor{.y = 10.0F, .z = 0.3F * static_cast<float>(chain)};
        const RigidBody* previous = nullptr;
        for (int index = 0; index < LINKS; ++index)
        {
            const float x = PITCH * static_cast<float>(index);
            RigidBody& body = world.add({.pose = {.position = anchor + Vector3{.x = x + 0.5F * PITCH}, .orientation = along_x}, .angular_damping = 0.1F},
                                        {.shape = link, .filter = {.group = -1}});
            (void)world.join(ball_and_socket_joint(
                {.first = body.handle(), .second = previous != nullptr ? previous->handle() : RigidBodyHandle{}, .anchor = anchor + Vector3{.x = x}}));
            previous = &body;
            links.push_back(&body);
        }
    }
    const float fall_speed = std::sqrt(2.0F * STANDARD_GRAVITY * PITCH * static_cast<float>(LINKS));
    float fastest = 0.0F;
    for (int step = 0; step < 90; ++step)
    {
        world.world->step(STEP);
        for (const RigidBody* body : links)
        {
            const RigidBodyState state = body->state();
            ASSERT_TRUE(finite(state)) << "step " << step;
            fastest = std::max(fastest, gfx::length(state.linear_velocity));
        }
    }
    EXPECT_LT(fastest, 3.0F * fall_speed) << "the fastest link, against a fall from the chain's length at " << fall_speed << " m/s";
    EXPECT_GT(fastest, 0.3F * fall_speed) << "the chains swung";
    float widest = 0.0F;
    for (std::size_t index = 0; index + 1 < links.size(); ++index)
    {
        if ((index + 1) % LINKS == 0)
        {
            continue;
        }
        const Vector3 end = gfx::transform_point(links[index]->state().pose, Vector3{.y = -0.5F * PITCH});
        const Vector3 start = gfx::transform_point(links[index + 1]->state().pose, Vector3{.y = 0.5F * PITCH});
        widest = std::max(widest, gfx::length(end - start));
    }
    EXPECT_LT(widest, 0.03F) << "the joints hold";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, NewColorsJoinTheOverflowSetInsteadOfSpareColors)
{
    // OVERFLOW_NEW_COLORS: eight chains of 24 capsule links released level side by side 0.3 m apart (their links collide):
    // the links' speculative contacts, which cannot meet at their relative speed, keep an overflow set
    // (RELATIVE_SPEED_COLORING), so with the flag each step records the colors the previous step used (the joints' three,
    // not two spare ones): fewer dispatches (traced builds), no more replays, and the chains swing as without it (each link
    // within 2 cm). Then a hub with six rods on ball joints, the rods moving outward at 3 m/s, joins the world: its joints
    // need six colors, three of them new, whose joints the flag gives to the overflow set — solved in that step (each
    // joint within 1 cm; unsolved, they open by 5 cm). In the Jacobi mode nothing is colored: the same bits.
    constexpr int CHAINS = 8;
    constexpr int LINKS = 24;
    constexpr float PITCH = 0.36F;
    struct Run
    {
        std::vector<RigidBodyState> states{};
        double dispatches = 0.0;
        double replays = 0.0;
        float hub_gap = 0.0F;
    };
    const auto run = [&](GpuOptimization optimizations) {
        Scene world = scene({}, optimizations);
        std::vector<RigidBody*> links;
        const std::shared_ptr<const Shape> link = make_shape(CapsuleShape{.half_height = 0.15F, .radius = 0.06F});
        const Quaternion along_x = Quaternion::from_axis_angle({.z = 1.0F}, 0.5F * PI);
        for (int chain = 0; chain < CHAINS; ++chain)
        {
            const Vector3 anchor{.y = 10.0F, .z = 0.3F * static_cast<float>(chain)};
            const RigidBody* previous = nullptr;
            for (int index = 0; index < LINKS; ++index)
            {
                const float x = PITCH * static_cast<float>(index);
                RigidBody& body = world.add({.pose = {.position = anchor + Vector3{.x = x + 0.5F * PITCH}, .orientation = along_x}, .angular_damping = 0.1F},
                                            {.shape = link});
                (void)world.join(ball_and_socket_joint(
                    {.first = body.handle(), .second = previous != nullptr ? previous->handle() : RigidBodyHandle{}, .anchor = anchor + Vector3{.x = x}}));
                previous = &body;
                links.push_back(&body);
            }
        }
        const std::unique_ptr<gfx::profiler::Profiler> recorder = gfx::profiler::create_profiler();
        gfx::profiler::set_current_profiler(recorder.get());
        const gfx::profiler::SessionHandle session = recorder->create_session({.name = "new colors"});
        for (int step = 0; step < 90; ++step)
        {
            recorder->begin_frame(session);
            world.world->step(STEP);
            (void)recorder->end_frame(session);
        }
        const gfx::profiler::Trace trace = recorder->finalize_session(session);
        gfx::profiler::set_current_profiler(nullptr);
        Run result;
        for (const gfx::profiler::TraceFrame& frame : trace.frames())
        {
            for (const gfx::profiler::TraceSample& sample : frame.samples)
            {
                const std::string_view name = trace.sites()[sample.site].name;
                result.dispatches += name == "GPU XPBD: dispatches" ? sample.number() : 0.0;
                result.replays += name == "GPU XPBD: replay" ? 1.0 : 0.0;
            }
        }
        for (const RigidBody* body : links)
        {
            result.states.push_back(body->state());
        }

        // The hub: six rods on ball joints at its faces, moving outward; one step; how far each joint opened.
        const Vector3 center{.x = -4.0F, .y = 5.0F};
        RigidBody& hub = world.add({.pose = {.position = center}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
        const std::shared_ptr<const Shape> rod = box_shape({.x = 0.2F, .y = 0.03F, .z = 0.03F});
        std::vector<std::pair<RigidBody*, Vector3>> rods;
        for (int face = 0; face < 6; ++face)
        {
            std::array<float, 3> components{};
            components[static_cast<std::size_t>(face / 2)] = face % 2 == 0 ? 1.0F : -1.0F;
            const Vector3 direction{.x = components[0], .y = components[1], .z = components[2]};
            const Quaternion along = Quaternion::from_axis_angle({.z = 1.0F}, face / 2 == 1 ? 0.5F * PI : 0.0F) *
                                     Quaternion::from_axis_angle({.y = 1.0F}, face / 2 == 2 ? 0.5F * PI : 0.0F);
            RigidBody& body = world.add({.pose = {.position = center + direction * 0.3F, .orientation = along}, .linear_velocity = direction * 3.0F}, {.shape = rod});
            const Vector3 anchor = center + direction * 0.1F;
            (void)world.join(ball_and_socket_joint({.first = body.handle(), .second = hub.handle(), .anchor = anchor}));
            rods.emplace_back(&body, gfx::transform_point(gfx::inverse(body.state().pose), anchor));
        }
        const Transform hub_start = hub.state().pose;
        std::vector<Vector3> hub_anchors;
        for (int face = 0; face < 6; ++face)
        {
            std::array<float, 3> components{};
            components[static_cast<std::size_t>(face / 2)] = face % 2 == 0 ? 0.1F : -0.1F;
            hub_anchors.push_back(gfx::transform_point(gfx::inverse(hub_start), center + Vector3{.x = components[0], .y = components[1], .z = components[2]}));
        }
        world.world->step(STEP);
        for (std::size_t index = 0; index < rods.size(); ++index)
        {
            const Vector3 on_rod = gfx::transform_point(rods[index].first->state().pose, rods[index].second);
            const Vector3 on_hub = gfx::transform_point(hub.state().pose, hub_anchors[index]);
            result.hub_gap = std::max(result.hub_gap, gfx::length(on_rod - on_hub));
        }
        return result;
    };
    const Run new_colors = run(DEFAULT_GPU_OPTIMIZATIONS);
    const Run spare_colors = run(static_cast<GpuOptimization>(std::to_underlying(DEFAULT_GPU_OPTIMIZATIONS) &
                                                              ~std::to_underlying(GpuOptimization::OVERFLOW_NEW_COLORS)));
    float largest = 0.0F;
    for (std::size_t index = 0; index < new_colors.states.size(); ++index)
    {
        ASSERT_TRUE(finite(new_colors.states[index]));
        largest = std::max(largest, gfx::length(new_colors.states[index].pose.position - spare_colors.states[index].pose.position));
    }
    if (mode() == GpuSolverMode::JACOBI)
    {
        EXPECT_EQ(largest, 0.0F) << "nothing is colored in the Jacobi mode";
    }
    else
    {
        EXPECT_LT(largest, 0.02F) << "the chains swing as with spare colors";
        EXPECT_LT(new_colors.hub_gap, 0.01F) << "the hub's joints of new colors solved in the overflow set";
        EXPECT_LT(spare_colors.hub_gap, 0.01F) << "the hub's joints solved after a replay";
        if constexpr (gfx::profiler::PROFILER_ENABLED)
        {
            EXPECT_LT(new_colors.dispatches, spare_colors.dispatches) << "no spare colors recorded";
            EXPECT_LE(new_colors.replays, spare_colors.replays) << "no more replays";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdJointTest, IslandRoundsFollowWhatTheLastStepUsed)
{
    // RECORDED_PASSES_BY_USE: a chain of 16 boxes hanging on ball joints and 30 boxes resting apart on a floor: the islands'
    // hooks change labels in a few rounds only (traced builds: fewer than the 6 that would make 8 recorded), so the steps
    // record that many and two more — and replay no step after the first ones (a round count kept too small would replay
    // every step) — and the islands stay right: everything falls asleep, a push on one box wakes it alone, a push on the
    // chain's lowest link wakes its highest.
    Scene world = scene();
    world.add({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, {.shape = box_shape({.x = 30, .y = 0.5F, .z = 30})});
    std::vector<RigidBody*> boxes;
    for (int index = 0; index < 30; ++index)
    {
        boxes.push_back(&world.add({.pose = {.position = {.x = -8.0F + 0.8F * static_cast<float>(index % 10), .y = 0.2F, .z = -6.0F + 0.8F * static_cast<float>(index / 10)}}},
                                   {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})}));
    }
    std::vector<RigidBody*> links;
    for (int index = 0; index < 16; ++index)
    {
        const float y = 9.8F - 0.4F * static_cast<float>(index);
        links.push_back(&world.add({.pose = {.position = {.x = 6.0F, .y = y}}}, {.shape = box_shape({.x = 0.1F, .y = 0.2F, .z = 0.1F})}));
        const RigidBodyHandle above = index == 0 ? RigidBodyHandle{} : links[static_cast<std::size_t>(index - 1)]->handle();
        (void)world.join(ball_and_socket_joint({.first = links.back()->handle(), .second = above, .anchor = {.x = 6.0F, .y = y + 0.2F}}));
    }
    const std::unique_ptr<gfx::profiler::Profiler> recorder = gfx::profiler::create_profiler();
    gfx::profiler::set_current_profiler(recorder.get());
    const gfx::profiler::SessionHandle session = recorder->create_session({.name = "island rounds"});
    for (int step = 0; step < 240; ++step)
    {
        recorder->begin_frame(session);
        world.world->step(STEP);
        (void)recorder->end_frame(session);
    }
    const gfx::profiler::Trace trace = recorder->finalize_session(session);
    gfx::profiler::set_current_profiler(nullptr);
    double late_replays = 0.0;
    double most_rounds = 0.0;
    std::size_t frame_index = 0;
    for (const gfx::profiler::TraceFrame& frame : trace.frames())
    {
        for (const gfx::profiler::TraceSample& sample : frame.samples)
        {
            const std::string_view name = trace.sites()[sample.site].name;
            late_replays += name == "GPU XPBD: replay" && frame_index >= 10 ? 1.0 : 0.0;
            most_rounds = name == "GPU XPBD: island rounds" ? std::max(most_rounds, sample.number()) : most_rounds;
        }
        ++frame_index;
    }
    if constexpr (gfx::profiler::PROFILER_ENABLED)
    {
        EXPECT_EQ(late_replays, 0.0) << "no step replayed for its islands after the first ones";
        EXPECT_GT(most_rounds, 0.0) << "the chain's hooks changed labels";
        EXPECT_LT(most_rounds, 6.0) << "fewer rounds than the 8 recorded before";
    }
    for (const RigidBody* body : boxes)
    {
        EXPECT_FALSE(body->state().awake);
    }
    for (const RigidBody* link : links)
    {
        EXPECT_FALSE(link->state().awake);
    }
    boxes[7]->apply_linear_impulse({.y = 0.5F});
    links.back()->apply_linear_impulse({.x = 0.5F});
    world.world->step(STEP);
    EXPECT_TRUE(boxes[7]->state().awake) << "the pushed box";
    EXPECT_FALSE(boxes[8]->state().awake) << "a box apart stays asleep";
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
    // The default optimizations that keep the bits (all but REACH_BOXES and BVH_REFIT, which find the same pairs in another
    // order, RESTING_STEPS, whose woken contacts warm-start from an earlier step, REACHABLE_CONTACTS, which makes fewer
    // constraints, RELATIVE_SPEED_COLORING, which solves some contacts in the overflow set, OVERFLOW_NEW_COLORS, which
    // solves the constraints of new colors there, and PUSHED_CONTACT_CACHE, which keeps fewer friction anchors) against
    // none (the baseline) on a
    // scene whose layouts change while it runs — a joint disabled, a collider's material changed, a body and a collider
    // made, a joint breaking during a step — between steps whose edits keep them (forces, impulses, velocities): the same
    // states bit for bit every 15 steps, the same events.
    constexpr GpuOptimization SAME_BITS =
        static_cast<GpuOptimization>(std::to_underlying(DEFAULT_GPU_OPTIMIZATIONS) &
                                     ~(std::to_underlying(GpuOptimization::REACH_BOXES) | std::to_underlying(GpuOptimization::BVH_REFIT) |
                                       std::to_underlying(GpuOptimization::RESTING_STEPS) |
                                       std::to_underlying(GpuOptimization::REACHABLE_CONTACTS) |
                                       std::to_underlying(GpuOptimization::RELATIVE_SPEED_COLORING) |
                                       std::to_underlying(GpuOptimization::OVERFLOW_NEW_COLORS) |
                                       std::to_underlying(GpuOptimization::PUSHED_CONTACT_CACHE)));
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
    Scene optimized = scene({}, SAME_BITS);
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
