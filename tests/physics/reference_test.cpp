// The reference backend: the Box3D backend's scenarios (free fall, a stack that rests and sleeps, a pendulum's period,
// limits, a rope, a breaking weld, events, a kinematic platform, determinism on any number of threads), what only it
// simulates (six-DOF joints, motors, dynamic meshes, separate static and dynamic friction, rolling resistance,
// restitution, continuous bodies, user constraints), its components replaced one by one, its soft bodies.
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

constexpr float STEP = 1.0F / 60.0F;
constexpr float PI = std::numbers::pi_v<float>;

[[nodiscard]] auto
sphere(float radius) -> std::shared_ptr<const Shape>
{
    return make_shape(SphereShape{.radius = radius});
}

[[nodiscard]] auto
box(Vector3 half_extents) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half_extents});
}

[[nodiscard]] auto
reference_world(const PhysicsWorldDescription& description = {}) -> std::unique_ptr<PhysicsWorld>
{
    return create_physics_world(description, PhysicsBackendKind::REFERENCE);
}

/// A static floor (a box whose top is at y = 0).
struct Floor
{
    std::unique_ptr<RigidBody> body;
    std::unique_ptr<Collider> collider;
};

[[nodiscard]] auto
make_floor(PhysicsWorld& world,
           const SurfaceMaterial& material = {}) -> Floor
{
    Floor floor{.body = world.create_rigid_body({.kind = BodyKind::STATIC}), .collider = nullptr};
    floor.collider = floor.body->add_collider({.shape = box({.x = 50, .y = 0.5F, .z = 50}), .pose = {.position = {.y = -0.5F}}, .material = material});
    return floor;
}

/// A dynamic body with one collider.
struct Body
{
    std::unique_ptr<RigidBody> body;
    std::unique_ptr<Collider> collider;
};

[[nodiscard]] auto
make_body(PhysicsWorld& world,
          const RigidBodyDescription& description,
          const ColliderDescription& collider) -> Body
{
    Body made{.body = world.create_rigid_body(description), .collider = nullptr};
    made.collider = made.body->add_collider(collider);
    return made;
}

void
run(PhysicsWorld& world,
    int steps)
{
    for (int step = 0; step < steps; ++step)
    {
        world.step(STEP);
    }
}

// ===================================================================================================== the Box3D scenarios

TEST(ReferenceTest, TheFactoryMakesReferenceWorldsOfTheAbstractionsComponents)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    EXPECT_TRUE(world->backend() == PhysicsBackendKind::REFERENCE);
    EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::BVH);
    EXPECT_TRUE(world->broad_phase().is_external());
    EXPECT_EQ(world->narrow_phase().kind(), NarrowPhaseKind::GJK_EPA);
    EXPECT_EQ(world->solver().kind(), SolverKind::SEQUENTIAL_IMPULSE);
    EXPECT_TRUE(world->solver().is_external());
    EXPECT_EQ(world->integrator().kind(), IntegratorKind::SEMI_IMPLICIT_EULER);
    EXPECT_EQ(world->soft_body_integrator().kind(), IntegratorKind::XPBD);
}

TEST(ReferenceTest, ABodyFallsFreely)
{
    // Semi-implicit Euler over the four sub-steps: the velocity is exact, the drop a sub-step's worth more than ½ g t².
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body ball = make_body(*world, {.pose = {.position = {.y = 100}}, .angular_damping = 0.0F}, {.shape = sphere(0.5F)});
    run(*world, 60);
    const RigidBodyState state = ball.body->state();
    EXPECT_NEAR(state.linear_velocity.y, -STANDARD_GRAVITY, 1e-3F);
    EXPECT_NEAR(100.0F - state.pose.position.y, 0.5F * STANDARD_GRAVITY * (1.0F + STEP / 4.0F), 1e-3F);
    EXPECT_NEAR(ball.body->mass_properties().mass, 4.0F / 3.0F * PI * 0.125F * 1000.0F, 1.0F);
}

TEST(ReferenceTest, AStackComesToRestAndFallsAsleep)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Floor floor = make_floor(*world);
    std::vector<Body> boxes;
    for (int level = 0; level < 5; ++level)
    {
        boxes.push_back(make_body(*world, {.pose = {.position = {.y = 0.5F + static_cast<float>(level) * 1.01F}}}, {.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
    }
    bool fell_asleep = false;
    for (int step = 0; step < 300; ++step)
    {
        world->step(STEP);
        fell_asleep = fell_asleep || std::ranges::any_of(world->events().sleeps, [](const SleepEvent& event) { return event.kind == SleepEventKind::FELL_ASLEEP; });
    }
    for (std::size_t level = 0; level < boxes.size(); ++level)
    {
        const RigidBodyState state = boxes[level].body->state();
        EXPECT_NEAR(state.pose.position.y, 0.5F + static_cast<float>(level), 0.03F) << "level " << level;
        EXPECT_NEAR(state.pose.position.x, 0.0F, 0.01F) << "level " << level;
        EXPECT_LT(length(state.linear_velocity), 0.05F);
    }
    EXPECT_TRUE(fell_asleep);
    EXPECT_FALSE(boxes.front().body->state().awake);

    // The bottom box touches the floor and the box above, four points each.
    std::array<ContactInfo, 8> contacts{};
    const QueryResult<std::span<ContactInfo>> touching = world->contacts_of(boxes.front().body->handle(), contacts);
    ASSERT_TRUE(touching.has_value());
    EXPECT_EQ(touching->size(), 2U);
    for (const ContactInfo& contact : *touching)
    {
        EXPECT_EQ(contact.collider, boxes.front().collider->handle());
        EXPECT_GE(contact.manifold.point_count, 3U);
    }

    // A push wakes the stack's bottom box and, through its island, the others.
    boxes.front().body->apply_linear_impulse({.x = 1.0F});
    world->step(STEP);
    EXPECT_TRUE(boxes.back().body->state().awake);
}

TEST(ReferenceTest, APendulumSwingsWithItsPeriod)
{
    // A small heavy bob 1 m below a hinge to the world, released at 5 degrees: T = 2π √(L / g).
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    constexpr float LENGTH = 1.0F;
    constexpr float START = 5.0F * PI / 180.0F;
    const Vector3 bob_position{.x = LENGTH * std::sin(START), .y = 2.0F - LENGTH * std::cos(START)};
    const Body bob = make_body(*world, {.pose = {.position = bob_position}, .angular_damping = 0.0F}, {.shape = sphere(0.02F), .density = 20000.0F});
    const std::unique_ptr<Joint> hinge = world->create_joint(hinge_joint({.first = bob.body->handle(), .anchor = {.y = 2.0F}, .axis = {.z = 1.0F}}));
    std::vector<float> crossings;
    float previous = bob_position.x;
    for (int step = 1; step <= 60 * 7; ++step)
    {
        world->step(STEP);
        const float x = bob.body->state().pose.position.x;
        if (previous > 0.0F && x <= 0.0F)
        {
            crossings.push_back(static_cast<float>(step) * STEP - STEP * x / (x - previous));
        }
        previous = x;
    }
    ASSERT_GE(crossings.size(), 3U);
    const float period = (crossings.back() - crossings.front()) / static_cast<float>(crossings.size() - 1);
    EXPECT_NEAR(period, 2.0F * PI * std::sqrt(LENGTH / STANDARD_GRAVITY), 0.02F * 2.0F * PI * std::sqrt(LENGTH / STANDARD_GRAVITY));
    EXPECT_NEAR(length(bob.body->state().pose.position - Vector3{.y = 2.0F}), LENGTH, 0.01F) << "the hinge holds the distance";
}

TEST(ReferenceTest, JointsHoldTheirLimitsRopesTheirLengthAndWeldsBreak)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world();

    // An arm on a hinge limited to ±0.5 rad from horizontal falls to its lower limit.
    const Body arm = make_body(*world, {.pose = {.position = {.x = 1.0F, .y = 5.0F}}}, {.shape = box({.x = 1.0F, .y = 0.05F, .z = 0.05F})});
    const std::unique_ptr<Joint> hinge = world->create_joint(
        hinge_joint({.first = arm.body->handle(), .anchor = {.y = 5.0F}, .axis = {.z = 1.0F}, .limits = JointLimits{.lower = -0.5F, .upper = 0.5F}}));

    // A ball on a rope of 2 m from a point 10 m up falls to the rope's end.
    const Body ball = make_body(*world, {.pose = {.position = {.x = 5.0F, .y = 9.5F}}}, {.shape = sphere(0.1F)});
    const std::unique_ptr<Joint> rope = world->create_joint(
        distance_joint({.first = ball.body->handle(), .anchor_first = {}, .anchor_second = {.x = 5.0F, .y = 10.0F}, .length = 2.0F, .mode = DistanceMode::ROPE}));

    // A heavy block welded to the world with a weak weld breaks.
    const Body block = make_body(*world, {.pose = {.position = {.x = -5.0F, .y = 5.0F}}}, {.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .density = 5000.0F});
    const std::unique_ptr<Joint> weld = world->create_joint(fixed_joint({.first = block.body->handle(), .anchor = {.x = -5.0F, .y = 5.0F}, .breaking = {.force = 1000.0F}}));

    // A wheel on a hinge to the world with a motor spins up to its target speed (the world relative to the wheel: the
    // wheel turns at minus it, as Box3D's).
    const Body wheel = make_body(*world, {.pose = {.position = {.x = 10.0F, .y = 5.0F}}, .angular_damping = 0.0F}, {.shape = sphere(0.5F)});
    const std::unique_ptr<Joint> axle = world->create_joint(
        hinge_joint({.first = wheel.body->handle(), .anchor = {.x = 10.0F, .y = 5.0F}, .axis = {.z = 1.0F}, .motor = JointMotor{.target_speed = 3.0F, .maximum_force = 500.0F}}));

    bool broke = false;
    for (int step = 0; step < 180; ++step)
    {
        world->step(STEP);
        broke = broke || std::ranges::any_of(world->events().joints, [&weld](const JointEvent& event) { return event.joint == weld->handle(); });
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

TEST(ReferenceTest, JointsBreakUnderAShockWhateverTheSubSteps)
{
    // A crate falls with a lid on a hinge that holds the lid's weight (294 N) but not its stop when the crate lands,
    // which happens within one sub-step. A joint's force is the step's average (the impulse it gave over the step, per
    // second), so the shock counts on any number of sub-steps (the example found the hinge holding when only the last
    // sub-step's force counted). A steady load gives the same force on any number of sub-steps: a block's weight.
    for (const std::uint32_t sub_steps : {1U, 4U, 8U})
    {
        const std::unique_ptr<PhysicsWorld> world = reference_world({.solver = {.sub_steps = sub_steps}});
        const Floor floor = make_floor(*world);
        const Body crate = make_body(*world, {.pose = {.position = {.y = 4.0F}}}, {.shape = box({.x = 0.5F, .y = 0.4F, .z = 0.5F}), .density = 500.0F});
        const Body lid = make_body(*world, {.pose = {.position = {.y = 4.45F}}}, {.shape = box({.x = 0.5F, .y = 0.05F, .z = 0.5F}), .density = 300.0F});
        const std::unique_ptr<Joint> hinge = world->create_joint(hinge_joint({.first = crate.body->handle(),
                                                                              .second = lid.body->handle(),
                                                                              .anchor = {.y = 4.4F, .z = -0.5F},
                                                                              .axis = {.x = 1.0F},
                                                                              .limits = JointLimits{.lower = -1.2F, .upper = 0.0F},
                                                                              .breaking = {.force = 800.0F}}));
        const Body block = make_body(*world, {.pose = {.position = {.x = 5.0F, .y = 4.0F}}}, {.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
        const std::unique_ptr<Joint> hook = world->create_joint(ball_and_socket_joint({.first = block.body->handle(), .anchor = {.x = 5.0F, .y = 5.0F}}));

        int landed = 0;
        int broke = 0;
        for (int step = 1; step <= 120; ++step)
        {
            world->step(STEP);
            for (const ContactEvent& event : world->events().contacts)
            {
                const bool crate_and_floor = (event.first == crate.collider->handle() && event.second == floor.collider->handle())
                                          || (event.first == floor.collider->handle() && event.second == crate.collider->handle());
                if (crate_and_floor && event.kind == ContactEventKind::BEGIN && landed == 0)
                {
                    landed = step;
                }
            }
            if (std::ranges::any_of(world->events().joints, [&hinge](const JointEvent& event) { return event.joint == hinge->handle(); }))
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

TEST(ReferenceTest, ContactAndTriggerEventsAreReported)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world({.hit_event_speed = 1.0F});
    const Floor floor = make_floor(*world);
    const std::unique_ptr<RigidBody> ball = world->create_rigid_body({.pose = {.position = {.y = 3.0F}}});
    std::unique_ptr<Collider> collider = ball->add_collider({.shape = sphere(0.25F)});
    const ColliderHandle ball_handle = collider->handle();
    const std::unique_ptr<Trigger> zone = world->create_trigger({.shape = box({.x = 1.0F, .y = 0.5F, .z = 1.0F}), .pose = {.position = {.y = 1.5F}}});
    std::vector<ContactEvent> contacts;
    std::vector<TriggerEvent> triggers;
    for (int step = 0; step < 90; ++step)
    {
        world->step(STEP);
        contacts.insert(contacts.end(), world->events().contacts.begin(), world->events().contacts.end());
        triggers.insert(triggers.end(), world->events().triggers.begin(), world->events().triggers.end());
    }
    const auto count = [](const auto& events, auto kind) { return std::ranges::count_if(events, [kind](const auto& event) { return event.kind == kind; }); };
    EXPECT_EQ(count(contacts, ContactEventKind::BEGIN), 1);
    EXPECT_GE(count(contacts, ContactEventKind::HIT), 1);
    EXPECT_EQ(count(triggers, TriggerEventKind::ENTER), 1);
    EXPECT_EQ(count(triggers, TriggerEventKind::EXIT), 1);
    EXPECT_GE(count(triggers, TriggerEventKind::STAY), 1);
    const auto begin = std::ranges::find(contacts, ContactEventKind::BEGIN, &ContactEvent::kind);
    ASSERT_NE(begin, contacts.end());
    EXPECT_EQ(std::minmax(begin->first, begin->second), std::minmax(floor.collider->handle(), ball_handle));
    const auto enter = std::ranges::find(triggers, TriggerEventKind::ENTER, &TriggerEvent::kind);
    EXPECT_EQ(enter->trigger, zone->handle());
    EXPECT_EQ(enter->collider, ball_handle);

    // Destroying the ball's collider ends its touch at the next step.
    collider.reset();
    world->step(STEP);
    EXPECT_EQ(count(std::vector(world->events().contacts.begin(), world->events().contacts.end()), ContactEventKind::END), 1);
}

TEST(ReferenceTest, AKinematicPlatformLiftsACrate)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body platform = make_body(*world, {.kind = BodyKind::KINEMATIC, .pose = {.position = {.y = 0.25F}}}, {.shape = box({.x = 2, .y = 0.25F, .z = 2})});
    const Body crate = make_body(*world, {.pose = {.position = {.y = 1.0F}}}, {.shape = box({.x = 0.25F, .y = 0.25F, .z = 0.25F})});
    run(*world, 60);
    for (int step = 0; step < 120; ++step)
    {
        platform.body->move_kinematic({.position = {.y = 0.25F + static_cast<float>(step + 1) * 0.01F}}, STEP);
        world->step(STEP);
    }
    EXPECT_NEAR(platform.body->state().pose.position.y, 1.45F, 1e-3F);
    EXPECT_NEAR(crate.body->state().pose.position.y, 1.45F + 0.25F + 0.25F, 0.02F) << "carried up";
}

/// The poses of a small scene of boxes and spheres thrown onto a floor after 120 steps.
[[nodiscard]] auto
simulate_scene(TaskScheduler* scheduler) -> std::vector<Transform>
{
    const std::unique_ptr<PhysicsWorld> world = reference_world({.task_scheduler = scheduler});
    const Floor floor = make_floor(*world);
    std::vector<Body> bodies;
    for (int index = 0; index < 60; ++index)
    {
        const float x = static_cast<float>(index % 6) - 2.5F;
        const float z = static_cast<float>((index / 6) % 5) - 2.0F;
        bodies.push_back(make_body(*world,
                                   {.pose = {.position = {.x = x * 1.1F, .y = 1.0F + static_cast<float>(index / 30) * 1.2F, .z = z * 1.1F},
                                             .orientation = Quaternion::from_axis_angle({.y = 1}, 0.1F * static_cast<float>(index))},
                                    .linear_velocity = {.x = 0.3F * static_cast<float>(index % 3 - 1)},
                                    .angular_velocity = {.y = 0.5F}},
                                   {.shape = index % 2 == 0 ? box({.x = 0.4F, .y = 0.4F, .z = 0.4F}) : sphere(0.45F)}));
    }
    run(*world, 120);
    std::vector<Transform> poses;
    for (const Body& body : bodies)
    {
        poses.push_back(body.body->state().pose);
    }
    return poses;
}

TEST(ReferenceTest, SimulationsAreDeterministicWhateverTheThreads)
{
    const std::vector<Transform> first = simulate_scene(nullptr);
    const std::vector<Transform> second = simulate_scene(nullptr);
    ThreadTaskScheduler threads{4};
    const std::vector<Transform> threaded = simulate_scene(&threads);
    ASSERT_EQ(first.size(), threaded.size());
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        EXPECT_EQ((std::bit_cast<std::array<std::uint32_t, 3>>(first[index].position)), (std::bit_cast<std::array<std::uint32_t, 3>>(second[index].position))) << index;
        EXPECT_EQ((std::bit_cast<std::array<std::uint32_t, 3>>(first[index].position)), (std::bit_cast<std::array<std::uint32_t, 3>>(threaded[index].position))) << index;
        EXPECT_EQ(first[index].orientation, threaded[index].orientation) << index;
        EXPECT_GT(first[index].position.y, 0.0F) << "on the floor, not through it";
    }
}

// ===================================================================================================== what only the reference simulates

TEST(ReferenceTest, SixDegreesOfFreedomJointsKeepTheirLockedAxes)
{
    // A box free to slide along x only, thrown in every direction and spun: it slides along x, nothing else.
    const std::unique_ptr<PhysicsWorld> world = reference_world({.gravity = {}});
    const Body slider = make_body(*world, {.pose = {.position = {.y = 2.0F}}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    std::array<AxisMotion, 6> motion{};
    motion.fill(AxisMotion::LOCKED);
    motion[std::to_underlying(JointAxis::LINEAR_X)] = AxisMotion::FREE;
    const std::unique_ptr<Joint> joint = world->create_joint(six_degrees_of_freedom_joint({.first = slider.body->handle(), .frame = {.position = {.y = 2.0F}}, .motion = motion}));
    slider.body->set_linear_velocity({.x = 1.0F, .y = 1.0F, .z = 1.0F});
    slider.body->set_angular_velocity({.x = 0.5F, .y = 2.0F, .z = 0.3F});

    // A box that may only turn about y, within ±0.3 rad, spun about every axis: it stops at its limit, in place.
    const Body turner = make_body(*world, {.pose = {.position = {.x = 5.0F, .y = 2.0F}}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    std::array<AxisMotion, 6> turning{};
    turning.fill(AxisMotion::LOCKED);
    turning[std::to_underlying(JointAxis::ANGULAR_Y)] = AxisMotion::LIMITED;
    std::array<JointLimits, 6> limits{};
    limits[std::to_underlying(JointAxis::ANGULAR_Y)] = {.lower = -0.3F, .upper = 0.3F};
    const std::unique_ptr<Joint> hinge = world->create_joint(
        six_degrees_of_freedom_joint({.first = turner.body->handle(), .frame = {.position = {.x = 5.0F, .y = 2.0F}}, .motion = turning, .limits = limits}));
    turner.body->set_angular_velocity({.x = 1.0F, .y = 2.0F, .z = 1.0F});
    run(*world, 60);

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

TEST(ReferenceTest, MeshesFallOnMovingBodiesAndFrictionsDiffer)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Floor floor = make_floor(*world, {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.2F}});

    // A dynamic body whose collider is a closed mesh (a tetrahedron, given mass properties) rests on the floor.
    const std::array<Vector3, 4> corners{Vector3{.x = -0.5F, .z = -0.5F}, Vector3{.x = 0.5F, .z = -0.5F}, Vector3{.z = 0.5F}, Vector3{.y = 0.7F}};
    const std::array<std::array<std::uint32_t, 3>, 4> faces{{{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}}};
    const auto mesh = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = faces, .two_sided = true}});
    const Body tetrahedron = make_body(*world, {.pose = {.position = {.x = -3.0F, .y = 1.0F}}, .mass_properties = mesh->mass_properties(500.0F)}, {.shape = mesh});

    // On a 20° slope (tan 20° = 0.36): static friction 0.8 holds a box; at dynamic 0.2, a pushed box keeps sliding.
    const Quaternion tilt = Quaternion::from_axis_angle({.z = 1}, 20.0F * PI / 180.0F);
    const std::unique_ptr<RigidBody> slope = world->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = 5.0F, .y = 2.0F}, .orientation = tilt}});
    const std::unique_ptr<Collider> ramp = slope->add_collider({.shape = box({.x = 4.0F, .y = 0.1F, .z = 2.0F}), .material = {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.2F}}});
    const Vector3 up = rotate(tilt, {.y = 1.0F});
    const Vector3 resting = Vector3{.x = 5.0F, .y = 2.0F} + up * 0.3F;
    const Body held = make_body(*world, {.pose = {.position = resting, .orientation = tilt}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F}),
                                                                                              .material = {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.2F}}});
    const Body pushed = make_body(*world, {.pose = {.position = resting + Vector3{.z = 1.0F}, .orientation = tilt}},
                                  {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F}), .material = {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.2F}}});
    run(*world, 30);
    pushed.body->set_linear_velocity(rotate(tilt, {.x = -2.0F}));
    run(*world, 60);
    EXPECT_NEAR(tetrahedron.body->state().pose.position.y, 0.0F, 0.05F) << "the mesh rests on its face";
    EXPECT_LT(length(held.body->state().pose.position - resting), 0.03F) << "static friction holds";
    EXPECT_GT(length(pushed.body->state().pose.position - (resting + Vector3{.z = 1.0F})), 1.0F) << "dynamic friction lets it slide";
}

TEST(ReferenceTest, BallsBounceAndRollingResistanceStopsThem)
{
    // A ball of restitution 0.8 dropped from 2 m bounces to about 0.8² of the height.
    const std::unique_ptr<PhysicsWorld> world = reference_world({.solver = {.restitution_threshold = 0.5F}});
    const Floor floor = make_floor(*world, {.restitution = 0.8F});
    const Body ball = make_body(*world, {.pose = {.position = {.y = 2.0F}}}, {.shape = sphere(0.1F), .material = {.restitution = 0.8F}});
    float bounce = 0.0F;
    bool rising = false;
    for (int step = 0; step < 120; ++step)
    {
        world->step(STEP);
        const RigidBodyState state = ball.body->state();
        rising = rising || state.linear_velocity.y > 0.0F;
        if (rising)
        {
            bounce = std::max(bounce, state.pose.position.y - 0.1F);
        }
    }
    EXPECT_NEAR(bounce, 0.64F * 1.9F, 0.15F * 1.9F);

    // Rolling balls: one without rolling resistance rolls on, one with it stops.
    const std::unique_ptr<PhysicsWorld> flat = reference_world();
    const Floor ground = make_floor(*flat, {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.8F, .rolling = 0.0F}});
    const Body free = make_body(*flat, {.pose = {.position = {.y = 0.25F}}, .linear_velocity = {.x = 2.0F}, .angular_damping = 0.0F}, {.shape = sphere(0.25F)});
    const Body braked = make_body(*flat, {.pose = {.position = {.y = 0.25F, .z = 3.0F}}, .linear_velocity = {.x = 2.0F}, .angular_damping = 0.0F},
                                  {.shape = sphere(0.25F), .material = {.friction = {.rolling = 0.02F}}});
    run(*flat, 300);
    EXPECT_GT(length(free.body->state().linear_velocity), 1.0F) << "rolling on";
    EXPECT_LT(length(braked.body->state().linear_velocity), 0.05F) << "stopped by rolling resistance";
}

TEST(ReferenceTest, ContinuousBodiesDoNotTunnelThroughThinWalls)
{
    // A small ball at 150 m/s toward a 5 cm wall: through it when discrete, stopped when continuous.
    for (const MotionQuality quality : {MotionQuality::DISCRETE, MotionQuality::CONTINUOUS})
    {
        const std::unique_ptr<PhysicsWorld> world = reference_world({.gravity = {}});
        const std::unique_ptr<RigidBody> wall = world->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = 5.0F}}});
        const std::unique_ptr<Collider> wall_collider = wall->add_collider({.shape = box({.x = 0.025F, .y = 2.0F, .z = 2.0F})});
        const Body bullet = make_body(*world, {.pose = {.position = {.x = 0.3F}}, .linear_velocity = {.x = 150.0F}, .motion_quality = quality}, {.shape = sphere(0.05F)});
        run(*world, 10);
        if (quality == MotionQuality::CONTINUOUS)
        {
            EXPECT_LT(bullet.body->state().pose.position.x, 5.0F) << "stopped by the wall";
        }
        else
        {
            EXPECT_GT(bullet.body->state().pose.position.x, 5.0F) << "through the wall";
        }
    }
}

TEST(ReferenceTest, SpinningBallsKeepTheirContactsUnderThem)
{
    // A ball spinning at 200 rad/s (0.8 radian per sub-step) dropped on the floor: its contact stays under it, so the pair
    // begins touching, touches at every step after (contacts_of), and the ball does not sink. Contact points kept fixed on
    // the ball's surface turned away with the spin: the separation grew by up to its radius, the pair never touched (no
    // events: the sandbox's shots lost theirs) and the ball sank into what it hit.
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Floor floor = make_floor(*world);
    const Body ball = make_body(*world, {.pose = {.position = {.y = 0.3F}}, .angular_velocity = {.x = 200.0F}, .angular_damping = 0.0F},
                                {.shape = sphere(0.25F), .density = 3000.0F});
    bool began = false;
    int touching_steps = 0;
    float lowest = std::numeric_limits<float>::max();
    for (int step = 0; step < 60; ++step)
    {
        world->step(STEP);
        for (const ContactEvent& event : world->events().contacts)
        {
            began = began || (event.kind == ContactEventKind::BEGIN && (event.first == ball.collider->handle() || event.second == ball.collider->handle()));
        }
        std::array<ContactInfo, 4> contacts{};
        const QueryResult<std::span<ContactInfo>> touching = world->contacts_of(ball.body->handle(), contacts);
        touching_steps += step >= 20 && touching && !touching->empty() ? 1 : 0;
        lowest = std::min(lowest, ball.body->state().pose.position.y);
    }
    EXPECT_TRUE(began) << "BEGIN of the ball and the floor";
    EXPECT_EQ(touching_steps, 40) << "touching at every step once landed";
    EXPECT_GT(lowest, 0.25F - 0.015F) << "not sunk into the floor (it rests within the solver's tolerance)";
}

TEST(ReferenceTest, FallingConvexShapesStayOnTheFloor)
{
    // The physics sandbox's showcase: three of each convex kind (a sphere, a capsule, a cylinder, a cone, a hull built at
    // compile time, a compound) dropped tilted, landing on and against each other. A cylinder resting 3 mm deep in the
    // floor and later a hull went through it: GJK took their shallow overlaps for slivers (GjkEpaTest.
    // ShallowOverlapsOfALargeBoxAreOverlaps) and the contacts pushed them out through the floor's bottom face.
    constexpr std::array<Vector3, 8> CORNERS{Vector3{.x = -0.5F, .y = -0.3F, .z = -0.5F}, Vector3{.x = 0.5F, .y = -0.3F, .z = -0.5F},
                                             Vector3{.x = -0.5F, .y = -0.3F, .z = 0.5F},  Vector3{.x = 0.5F, .y = -0.3F, .z = 0.5F},
                                             Vector3{.x = -0.25F, .y = 0.3F, .z = -0.25F}, Vector3{.x = 0.25F, .y = 0.3F, .z = -0.25F},
                                             Vector3{.x = -0.25F, .y = 0.3F, .z = 0.25F},  Vector3{.x = 0.25F, .y = 0.3F, .z = 0.25F}};
    constexpr auto PLINTH = convex_hull<8, 6, 24>(CORNERS);
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Floor floor = make_floor(*world);
    const std::array<std::shared_ptr<const Shape>, 6> shapes{
        sphere(0.4F), make_shape(CapsuleShape{.half_height = 0.35F, .radius = 0.25F}), make_shape(CylinderShape{.half_height = 0.3F, .radius = 0.35F}),
        make_shape(ConeShape{.half_height = 0.4F, .radius = 0.4F}), make_shape(HullShape(PLINTH)),
        make_shape(CompoundShape({CompoundChild{.shape = sphere(0.3F), .pose = {.position = {.x = -0.45F}}}, CompoundChild{.shape = sphere(0.3F), .pose = {.position = {.x = 0.45F}}},
                                  CompoundChild{.shape = make_shape(CapsuleShape{.half_height = 0.3F, .radius = 0.1F}),
                                                .pose = {.orientation = Quaternion::from_axis_angle({.z = 1.0F}, PI / 2)}}}))};
    std::vector<Body> bodies;
    for (std::size_t kind = 0; kind < shapes.size(); ++kind)
    {
        for (int copy = 0; copy < 3; ++copy)
        {
            const Vector3 position{.x = 1.0F + static_cast<float>(kind) * 1.2F, .y = 2.0F + static_cast<float>(copy) * 1.5F + static_cast<float>(kind % 2) * 0.6F,
                                   .z = -1.5F + static_cast<float>(copy) * 1.5F};
            const Quaternion tilt = Quaternion::from_axis_angle(normalized(Vector3{.x = 1.0F, .y = 0.3F, .z = static_cast<float>(copy)}), 0.4F * static_cast<float>(copy + 1));
            bodies.push_back(make_body(*world, {.pose = {.position = position, .orientation = tilt}}, {.shape = shapes[kind]}));
        }
    }
    float lowest = std::numeric_limits<float>::max();
    std::size_t lowest_body = 0;
    for (int step = 0; step < 180; ++step)
    {
        world->step(STEP);
        for (std::size_t index = 0; index < bodies.size(); ++index)
        {
            const float height = bodies[index].body->state().pose.position.y;
            if (height < lowest)
            {
                lowest = height;
                lowest_body = index;
            }
        }
    }
    EXPECT_GT(lowest, 0.0F) << "body " << lowest_body << " (kind " << lowest_body / 3 << ") went into the floor";
}

TEST(ReferenceTest, HitsThatBounceOffWithinAStepAreReported)
{
    // A bouncy continuous ball at 60 m/s toward a wall: its speculative contact stops and returns it within one step, so its
    // points never end a step within the slop; the pair still touched (the solver pushed it), so BEGIN, HIT and END come.
    const std::unique_ptr<PhysicsWorld> world = reference_world({.gravity = {}});
    const std::unique_ptr<RigidBody> wall = world->create_rigid_body({.kind = BodyKind::STATIC});
    const std::unique_ptr<Collider> wall_collider = wall->add_collider({.shape = box({.x = 2.0F, .y = 2.0F, .z = 0.1F})});
    const Body ball = make_body(*world, {.pose = {.position = {.z = 2.7F}}, .linear_velocity = {.z = -60.0F}, .motion_quality = MotionQuality::CONTINUOUS},
                                {.shape = sphere(0.25F), .material = {.restitution = 1.0F}});
    std::array<bool, ENUMERATOR_COUNT<ContactEventKind>> seen{};
    for (int step = 0; step < 10; ++step)
    {
        world->step(STEP);
        for (const ContactEvent& event : world->events().contacts)
        {
            if ((event.first == ball.collider->handle() && event.second == wall_collider->handle())
                || (event.first == wall_collider->handle() && event.second == ball.collider->handle()))
            {
                seen[std::to_underlying(event.kind)] = true;
            }
        }
    }
    EXPECT_GT(ball.body->state().linear_velocity.z, 50.0F) << "bounced back";
    EXPECT_TRUE(seen[std::to_underlying(ContactEventKind::BEGIN)]);
    EXPECT_TRUE(seen[std::to_underlying(ContactEventKind::HIT)]);
    EXPECT_TRUE(seen[std::to_underlying(ContactEventKind::END)]);
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

TEST(ReferenceTest, UserConstraintsHoldTheirBodiesAndBreak)
{
    // A ball held at 2 m by its constraint (sideways it moves freely); a heavy one breaks a weak constraint.
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body held = make_body(*world, {.pose = {.position = {.y = 2.0F}}, .linear_velocity = {.x = 1.0F}}, {.shape = sphere(0.1F)});
    const std::unique_ptr<Constraint> height = world->create_constraint({.first = held.body->handle(), .constraint = std::make_shared<HeightConstraint>(2.0F)});
    const Body heavy = make_body(*world, {.pose = {.position = {.x = 3.0F, .y = 2.0F}}}, {.shape = sphere(0.3F), .density = 5000.0F});
    const std::unique_ptr<Constraint> weak = world->create_constraint(
        {.first = heavy.body->handle(), .constraint = std::make_shared<HeightConstraint>(2.0F), .breaking = {.force = 100.0F}});
    EXPECT_EQ(height->bodies().first, held.body->handle());
    bool broke = false;
    for (int step = 0; step < 60; ++step)
    {
        world->step(STEP);
        broke = broke || std::ranges::any_of(world->events().joints, [&weak](const JointEvent& event) { return event.constraint == weak->handle(); });
    }
    EXPECT_NEAR(held.body->state().pose.position.y, 2.0F, 0.01F);
    EXPECT_NEAR(held.body->state().pose.position.x, 1.0F, 0.05F);
    EXPECT_NEAR(length(height->state().force), held.body->mass_properties().mass * STANDARD_GRAVITY, 0.05F * held.body->mass_properties().mass * STANDARD_GRAVITY)
        << "it carries the ball's weight";
    EXPECT_TRUE(broke);
    EXPECT_TRUE(weak->state().broken);
    EXPECT_LT(heavy.body->state().pose.position.y, 0.0F);

    // Disabled, the ball falls; destroyed with its body, the constraint's object reports it.
    height->set_enabled(false);
    run(*world, 30);
    EXPECT_LT(held.body->state().pose.position.y, 1.5F);
    EXPECT_THROW((void)world->create_constraint({.first = held.body->handle(), .constraint = nullptr}), PhysicsError);
    EXPECT_THROW((void)world->create_constraint({.first = held.body->handle(), .second = held.body->handle(), .constraint = std::make_shared<HeightConstraint>(1.0F)}),
                 PhysicsError);
}

// ===================================================================================================== components

/// A broad phase that counts its updates around a spatial broad phase on another structure.
template<class Structure>
class CountingBroadPhase final : public BroadPhase
{
public:
    CountingBroadPhase() noexcept
        : BroadPhase{PhysicsSubsystem::SPATIAL}
    {
    }

    std::uint32_t updates = 0;

    [[nodiscard]] auto kind() const noexcept -> BroadPhaseKind override
    {
        return m_inner.kind();
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    auto insert(ColliderHandle collider,
                const Aabb& box,
                const CollisionFilter& filter) -> SpatialProxyHandle override
    {
        return m_inner.insert(collider, box, filter);
    }

    void remove(SpatialProxyHandle proxy) override
    {
        m_inner.remove(proxy);
    }

    auto move(SpatialProxyHandle proxy,
              const Aabb& box,
              Vector3 displacement) -> bool override
    {
        return m_inner.move(proxy, box, displacement);
    }

    void set_filter(SpatialProxyHandle proxy,
                    const CollisionFilter& filter) override
    {
        m_inner.set_filter(proxy, filter);
    }

    void update(TaskScheduler* scheduler) override
    {
        ++updates;
        m_inner.update(scheduler);
    }

    [[nodiscard]] auto pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_inner.pairs();
    }

    [[nodiscard]] auto new_pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_inner.new_pairs();
    }

    [[nodiscard]] auto ended_pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_inner.ended_pairs();
    }

    void structure_nodes(std::vector<SpatialNode>& nodes) const override
    {
        m_inner.structure_nodes(nodes);
    }

private:
    SpatialBroadPhaseComponent<Structure> m_inner{};
};

/// A narrow phase that counts the pairs it collides.
class CountingNarrowPhase final : public NarrowPhase
{
public:
    CountingNarrowPhase() noexcept
        : NarrowPhase{PhysicsSubsystem::NARROW_PHASE}
    {
    }

    mutable std::atomic<std::uint32_t> pairs{0};

    [[nodiscard]] auto kind() const noexcept -> NarrowPhaseKind override
    {
        return NarrowPhaseKind::GJK_EPA;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void collide(const ShapeInstance& first,
                 const ShapeInstance& second,
                 ContactCollector& contacts,
                 const CollisionSettings& settings) const override
    {
        ++pairs;
        gfx::physics::collide(first, second, contacts, settings);
    }
};

/// A solver that counts its steps around sequential impulses.
class CountingSolver final : public Solver
{
public:
    CountingSolver() noexcept
        : Solver{PhysicsSubsystem::DYNAMICS}
    {
    }

    std::uint32_t steps = 0;
    std::size_t contacts = 0;

    [[nodiscard]] auto kind() const noexcept -> SolverKind override
    {
        return SolverKind::SEQUENTIAL_IMPULSE;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void solve(const SolverStep& step) override
    {
        ++steps;
        contacts = std::max(contacts, step.contacts.size());
        m_inner.solve(step);
    }

private:
    SequentialImpulseSolver m_inner{};
};

/// An integrator that doubles gravity (forces twice as strong).
class DoubleForceIntegrator final : public Integrator
{
public:
    DoubleForceIntegrator() noexcept
        : Integrator{PhysicsSubsystem::DYNAMICS}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override
    {
        return IntegratorKind::SEMI_IMPLICIT_EULER;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override
    {
        return {};
    }

    void predict(const IntegrationState& state,
                 float seconds) override
    {
        for (std::size_t index = 0; index < state.positions.size(); ++index)
        {
            state.velocities[index] += (state.gravity + state.forces[index] * state.inverse_masses[index]) * (2.0F * seconds);
        }
    }

    void finalize(const IntegrationState& /*state*/,
                  float /*seconds*/) override
    {
    }
};

TEST(ReferenceTest, EachComponentCanBeReplaced)
{
    // A stack on every broad phase, with the application's narrow phase and solver: the same rest, each component used.
    for (int structure = 0; structure < 3; ++structure)
    {
        SCOPED_TRACE(structure);
        const std::unique_ptr<PhysicsWorld> world = reference_world();
        const Floor floor = make_floor(*world);
        std::vector<Body> boxes;
        for (int level = 0; level < 3; ++level)
        {
            boxes.push_back(make_body(*world, {.pose = {.position = {.y = 0.5F + static_cast<float>(level) * 1.01F}}}, {.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
        }
        world->step(STEP);
        std::uint32_t updates = 0;
        if (structure == 0)
        {
            auto broad_phase = std::make_unique<CountingBroadPhase<SweepAndPrune<3>>>();
            auto* counting = broad_phase.get();
            world->replace_broad_phase(std::move(broad_phase));
            EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::SWEEP_AND_PRUNE);
            run(*world, 120);
            updates = counting->updates;
        }
        else if (structure == 1)
        {
            auto broad_phase = std::make_unique<CountingBroadPhase<Octree<>>>();
            auto* counting = broad_phase.get();
            world->replace_broad_phase(std::move(broad_phase));
            EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::OCTREE);
            run(*world, 120);
            updates = counting->updates;

            // The octree shows its cells: the floor's (a coarse level) and the boxes', each listed once.
            std::vector<SpatialNode> nodes;
            world->broad_phase().structure_nodes(nodes);
            EXPECT_EQ(std::ranges::fold_left(nodes, 0U, [](std::uint32_t sum, const SpatialNode& node) { return sum + node.proxies; }), 4U);
            EXPECT_TRUE(std::ranges::any_of(nodes, [](const SpatialNode& node) { return node.depth > 0; }));
        }
        else
        {
            auto narrow_phase = std::make_unique<CountingNarrowPhase>();
            auto solver = std::make_unique<CountingSolver>();
            auto* narrow = narrow_phase.get();
            auto* counting_solver = solver.get();
            world->replace_narrow_phase(std::move(narrow_phase));
            world->replace_solver(std::move(solver));
            run(*world, 120);
            EXPECT_GT(narrow->pairs.load(), 0U);
            EXPECT_EQ(counting_solver->steps, 4U * 120U) << "once per sub-step";
            EXPECT_GE(counting_solver->contacts, 3U);
            updates = 120;
        }
        EXPECT_EQ(updates, 120U);
        for (std::size_t level = 0; level < boxes.size(); ++level)
        {
            EXPECT_NEAR(boxes[level].body->state().pose.position.y, 0.5F + static_cast<float>(level), 0.03F) << "level " << level;
        }
    }

    // The integrator: twice the forces, twice the fall's speed; null components are refused.
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body ball = make_body(*world, {.pose = {.position = {.y = 100}}}, {.shape = sphere(0.5F)});
    world->replace_integrator(std::make_unique<DoubleForceIntegrator>());
    run(*world, 30);
    EXPECT_NEAR(ball.body->state().linear_velocity.y, -STANDARD_GRAVITY, 1e-2F) << "half a second at 2 g";
    world->replace_integrator(make_integrator(IntegratorKind::RUNGE_KUTTA_4));
    run(*world, 30);
    EXPECT_NEAR(ball.body->state().linear_velocity.y, -1.5F * STANDARD_GRAVITY, 1e-2F) << "then half a second at g";
    EXPECT_THROW(world->replace_broad_phase(nullptr), PhysicsError);
    EXPECT_THROW(world->replace_solver(nullptr), PhysicsError);
}

TEST(ReferenceTest, SoftBodiesCoupleWithReferenceBodies)
{
    // A 1 kg box hanging from a rope pinned to the world: the rope holds it (the framework's corrections on the records).
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body weight = make_body(*world, {.pose = {.position = {.y = 2}}}, {.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 125.0F, .filter = {.group = -1}});
    SoftBodyDescription description = rope_preset({.segments = 10, .length = 1.0F, .origin = {.y = 3}, .mass = 0.2F});
    description.pins = {pin_to_world(description, 0), pin_to_body(10, weight.body->handle(), {})};
    description.filter = {.group = -1};
    description.damping = 0.5F;
    add_long_range_attachments(description);
    const std::unique_ptr<SoftBody> rope = world->create_soft_body(description);
    run(*world, 300);
    EXPECT_NEAR(weight.body->state().pose.position.y, 2.0F, 0.03F);
    EXPECT_LT(length(weight.body->state().linear_velocity), 0.05F);
}

} // namespace
