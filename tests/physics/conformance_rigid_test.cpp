// The conformance suite, rigid bodies: on every backend, simulations with known outcomes — free fall, a stack that comes
// to rest and falls asleep, a kinematic platform carrying a crate, every convex kind landing on a floor and staying on
// it, a spinning ball keeping its contact, restitution and rolling resistance, continuous bodies stopped by thin walls,
// compounds, meshes and height fields, meshes on moving bodies and separate static and dynamic friction where the
// backend has them — and determinism (two runs, and on four threads where the backend uses them: the same bits).
#include "conformance_test.hpp"

namespace gfx::tests::conformance
{
namespace
{

using RigidConformanceTest = ConformanceTest;

TEST_P(RigidConformanceTest, WorldsNameTheirBackendAndRefuseInvalidDescriptions)
{
    const std::unique_ptr<PhysicsWorld> made = world();
    EXPECT_TRUE(made->backend() == backend());
    EXPECT_THROW((void)world({.gravity = {.y = std::numeric_limits<float>::quiet_NaN()}}), PhysicsError);
}

TEST_P(RigidConformanceTest, ABodyFallsFreely)
{
    // One second of free fall from 100 m: g reached, ½ g t² fallen (within each integrator's step), the mass of its
    // sphere at the default density published.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body ball = make_body(*made, {.pose = {.position = {.y = 100}}, .angular_damping = 0.0F}, {.shape = sphere(0.5F)});
    advance(*made, 60);
    const RigidBodyState state = ball.body->state();
    EXPECT_NEAR(state.linear_velocity.y, -STANDARD_GRAVITY, 1e-3F * STANDARD_GRAVITY);
    EXPECT_NEAR(100.0F - state.pose.position.y, 0.5F * STANDARD_GRAVITY, 0.01F * STANDARD_GRAVITY);
    EXPECT_NEAR(state.pose.position.x, 0.0F, 1e-6F);
    EXPECT_NEAR(ball.body->mass_properties().mass, 4.0F / 3.0F * PI * 0.125F * 1000.0F, 1.0F);
}

TEST_P(RigidConformanceTest, AStackComesToRestAndFallsAsleep)
{
    // Five boxes stacked on the floor: at rest where they were put (within 3 cm), asleep; the bottom one touches the floor
    // and the box above (three points or more each); a push wakes the stack (within a few steps: islands may wake a layer
    // at a time).
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body floor = make_floor(*made);
    std::vector<Body> boxes;
    for (int level = 0; level < 5; ++level)
    {
        boxes.push_back(make_body(*made, {.pose = {.position = {.y = 0.5F + static_cast<float>(level) * 1.01F}}}, {.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
    }
    bool fell_asleep = false;
    for (int step = 0; step < 300; ++step)
    {
        made->step(STEP);
        fell_asleep = fell_asleep || std::ranges::any_of(made->events().sleeps, [](const SleepEvent& event) { return event.kind == SleepEventKind::FELL_ASLEEP; });
    }
    for (std::size_t level = 0; level < boxes.size(); ++level)
    {
        const RigidBodyState state = boxes[level].body->state();
        EXPECT_NEAR(state.pose.position.y, 0.5F + static_cast<float>(level), 0.03F) << "level " << level;
        EXPECT_NEAR(state.pose.position.x, 0.0F, 0.01F) << "level " << level;
        EXPECT_LT(length(state.linear_velocity), 0.05F) << "level " << level;
    }
    EXPECT_TRUE(fell_asleep);
    EXPECT_FALSE(boxes.front().body->state().awake);

    const std::vector<ContactInfo> touching = contacts_of(*made, *boxes.front().body);
    EXPECT_EQ(touching.size(), 2U);
    for (const ContactInfo& contact : touching)
    {
        EXPECT_EQ(contact.collider, boxes.front().collider->handle());
        EXPECT_GE(contact.manifold.point_count, 3U);
    }

    boxes.front().body->apply_linear_impulse({.x = 1.0F});
    bool woke = false;
    for (int step = 0; step < 6 && !woke; ++step)
    {
        made->step(STEP);
        woke = boxes.back().body->state().awake;
    }
    EXPECT_TRUE(woke) << "the push woke the top of the stack";
}

TEST_P(RigidConformanceTest, KinematicBodiesReachTheirTargetsAndCarryOthers)
{
    // A kinematic platform moved up 1.2 m in two seconds reaches each target; the crate on it rides along.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body platform = make_body(*made, {.kind = BodyKind::KINEMATIC, .pose = {.position = {.y = 0.25F}}}, {.shape = box({.x = 2, .y = 0.25F, .z = 2})});
    const Body crate = make_body(*made, {.pose = {.position = {.y = 1.0F}}}, {.shape = box({.x = 0.25F, .y = 0.25F, .z = 0.25F})});
    advance(*made, 60);
    for (int step = 0; step < 120; ++step)
    {
        platform.body->move_kinematic({.position = {.y = 0.25F + static_cast<float>(step + 1) * 0.01F}}, STEP);
        made->step(STEP);
    }
    EXPECT_NEAR(platform.body->state().pose.position.y, 1.45F, 1e-3F);
    EXPECT_NEAR(crate.body->state().pose.position.y, 1.45F + 0.25F + 0.25F, 0.03F) << "carried up";
}

TEST_P(RigidConformanceTest, FallingConvexShapesStayOnTheFloor)
{
    // Three of each convex kind (sphere, capsule, cylinder, cone, a hull built at compile time, a compound) dropped
    // tilted onto the floor and onto each other: none goes into the floor.
    constexpr std::array<Vector3, 8> CORNERS{Vector3{.x = -0.5F, .y = -0.3F, .z = -0.5F}, Vector3{.x = 0.5F, .y = -0.3F, .z = -0.5F},
                                             Vector3{.x = -0.5F, .y = -0.3F, .z = 0.5F},  Vector3{.x = 0.5F, .y = -0.3F, .z = 0.5F},
                                             Vector3{.x = -0.25F, .y = 0.3F, .z = -0.25F}, Vector3{.x = 0.25F, .y = 0.3F, .z = -0.25F},
                                             Vector3{.x = -0.25F, .y = 0.3F, .z = 0.25F},  Vector3{.x = 0.25F, .y = 0.3F, .z = 0.25F}};
    constexpr auto PLINTH = convex_hull<8, 6, 24>(CORNERS);
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body floor = make_floor(*made);
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
            bodies.push_back(make_body(*made, {.pose = {.position = position, .orientation = tilt}}, {.shape = shapes[kind]}));
        }
    }
    float lowest = std::numeric_limits<float>::max();
    std::size_t lowest_body = 0;
    for (int step = 0; step < 180; ++step)
    {
        made->step(STEP);
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

TEST_P(RigidConformanceTest, SpinningBallsKeepTheirContactsUnderThem)
{
    // A ball spinning at 200 rad/s dropped on the floor: its contact begins, stays at every step once landed, and the ball
    // does not sink.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body floor = make_floor(*made);
    const Body ball = make_body(*made, {.pose = {.position = {.y = 0.3F}}, .angular_velocity = {.x = 200.0F}, .angular_damping = 0.0F},
                                {.shape = sphere(0.25F), .density = 3000.0F});
    bool began = false;
    int touching_steps = 0;
    float lowest = std::numeric_limits<float>::max();
    for (int step = 0; step < 60; ++step)
    {
        made->step(STEP);
        for (const ContactEvent& event : made->events().contacts)
        {
            began = began || (event.kind == ContactEventKind::BEGIN && between(event, ball.collider->handle(), floor.collider->handle()));
        }
        touching_steps += step >= 20 && !contacts_of(*made, *ball.body).empty() ? 1 : 0;
        lowest = std::min(lowest, ball.body->state().pose.position.y);
    }
    EXPECT_TRUE(began) << "BEGIN of the ball and the floor";
    EXPECT_EQ(touching_steps, 40) << "touching at every step once landed";
    EXPECT_GT(lowest, 0.25F - 0.015F) << "not sunk into the floor";
}

TEST_P(RigidConformanceTest, BallsBounceAndRollingResistanceStopsThem)
{
    // A ball of restitution 0.8 dropped from 2 m bounces to about 0.8² of the height; on a flat floor a rolling ball rolls
    // on, one with rolling resistance stops.
    const std::unique_ptr<PhysicsWorld> made = world({.solver = {.restitution_threshold = 0.5F}});
    const Body floor = make_floor(*made, {.restitution = 0.8F});
    const Body ball = make_body(*made, {.pose = {.position = {.y = 2.0F}}}, {.shape = sphere(0.1F), .material = {.restitution = 0.8F}});
    float bounce = 0.0F;
    bool rising = false;
    for (int step = 0; step < 120; ++step)
    {
        made->step(STEP);
        const RigidBodyState state = ball.body->state();
        rising = rising || state.linear_velocity.y > 0.0F;
        if (rising)
        {
            bounce = std::max(bounce, state.pose.position.y - 0.1F);
        }
    }
    EXPECT_NEAR(bounce, 0.64F * 1.9F, 0.15F * 1.9F);

    const std::unique_ptr<PhysicsWorld> flat = world();
    const Body ground = make_floor(*flat, {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.8F, .rolling = 0.0F}});
    const Body free = make_body(*flat, {.pose = {.position = {.y = 0.25F}}, .linear_velocity = {.x = 2.0F}, .angular_damping = 0.0F}, {.shape = sphere(0.25F)});
    const Body braked = make_body(*flat, {.pose = {.position = {.y = 0.25F, .z = 3.0F}}, .linear_velocity = {.x = 2.0F}, .angular_damping = 0.0F},
                                  {.shape = sphere(0.25F), .material = {.friction = {.rolling = 0.02F}}});
    advance(*flat, 300);
    EXPECT_GT(length(free.body->state().linear_velocity), 1.0F) << "rolling on";
    EXPECT_LT(length(braked.body->state().linear_velocity), 0.05F) << "stopped by rolling resistance";
}

TEST_P(RigidConformanceTest, ContinuousBodiesDoNotTunnelThroughThinWalls)
{
    // A small continuous ball at 150 m/s toward a 5 cm wall is stopped by it.
    const std::unique_ptr<PhysicsWorld> made = world({.gravity = {}});
    const Body wall = make_body(*made, {.kind = BodyKind::STATIC, .pose = {.position = {.x = 5.0F}}}, {.shape = box({.x = 0.025F, .y = 2.0F, .z = 2.0F})});
    const Body bullet = make_body(*made, {.pose = {.position = {.x = 0.3F}}, .linear_velocity = {.x = 150.0F}, .motion_quality = MotionQuality::CONTINUOUS},
                                  {.shape = sphere(0.05F)});
    advance(*made, 10);
    EXPECT_LT(bullet.body->state().pose.position.x, 5.0F) << "stopped by the wall";
    EXPECT_GT(bullet.body->state().pose.position.x, 4.9F) << "at the wall";
}

TEST_P(RigidConformanceTest, CompoundsMeshesAndHeightFieldsCollide)
{
    // A dumbbell (a compound of two spheres) rests on a static height field; a ball pulled up (negative gravity scale)
    // stops under the back of a static two-sided mesh.
    const std::unique_ptr<PhysicsWorld> made = world();
    std::array<float, 25> heights{};
    const auto field = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 5, 5}, .scale = {.x = 4, .y = 1, .z = 4}}});
    const Body terrain = make_body(*made, {.kind = BodyKind::STATIC, .pose = {.position = {.x = -8, .z = -8}}}, {.shape = field});
    const std::array<Vector3, 4> corners{Vector3{.x = 20, .y = 5, .z = -1}, {.x = 22, .y = 5, .z = -1}, {.x = 22, .y = 5, .z = 1}, {.x = 20, .y = 5, .z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 2> quad{{{0, 2, 1}, {0, 3, 2}}};
    const Body ceiling = make_body(*made, {.kind = BodyKind::STATIC}, {.shape = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = quad, .two_sided = true}})});
    const Body dumbbell = make_body(*made, {.pose = {.position = {.y = 2}}},
                                    {.shape = make_shape(CompoundShape{{CompoundChild{.shape = sphere(0.5F), .pose = {.position = {.x = -1}}},
                                                                        CompoundChild{.shape = sphere(0.5F), .pose = {.position = {.x = 1}}}}})});
    const Body balloon = make_body(*made, {.pose = {.position = {.x = 21, .y = 3}}, .gravity_scale = -1.0F}, {.shape = sphere(0.5F)});
    advance(*made, 180);
    EXPECT_NEAR(dumbbell.body->state().pose.position.y, 0.5F, 0.02F);
    EXPECT_NEAR(dumbbell.body->mass_properties().mass, 2.0F * 4.0F / 3.0F * PI * 0.125F * 1000.0F, 2.0F);
    EXPECT_NEAR(balloon.body->state().pose.position.y, 4.5F, 0.02F) << "stopped by the mesh's back side";
}

TEST_P(RigidConformanceTest, MeshesFallOnMovingBodiesAndFrictionsDiffer)
{
    // Where the backend has them: a dynamic body whose collider is a closed mesh rests on the floor; on a 20° slope static
    // friction 0.8 holds a box, and at dynamic friction 0.2 a pushed box keeps sliding.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body floor = make_floor(*made);
    const std::array<Vector3, 4> corners{Vector3{.x = -0.5F, .z = -0.5F}, Vector3{.x = 0.5F, .z = -0.5F}, Vector3{.z = 0.5F}, Vector3{.y = 0.7F}};
    const std::array<std::array<std::uint32_t, 3>, 4> faces{{{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}}};
    const auto mesh = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = faces, .two_sided = true}});
    std::optional<Body> tetrahedron;
    if (supports(PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, [&] {
            tetrahedron = make_body(*made, {.pose = {.position = {.x = -3.0F, .y = 1.0F}}, .mass_properties = mesh->mass_properties(500.0F)}, {.shape = mesh});
        }))
    {
        advance(*made, 90);
        EXPECT_NEAR(tetrahedron->body->state().pose.position.y, 0.0F, 0.05F) << "the mesh rests on its face";
    }

    const Friction friction{.static_coefficient = 0.8F, .dynamic_coefficient = 0.2F};
    const Quaternion tilt = Quaternion::from_axis_angle({.z = 1}, 20.0F * PI / 180.0F);
    const std::unique_ptr<RigidBody> slope = made->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = 5.0F, .y = 2.0F}, .orientation = tilt}});
    std::unique_ptr<Collider> ramp;
    if (!supports(PhysicsCapability::SEPARATE_STATIC_FRICTION,
                  [&] { ramp = slope->add_collider({.shape = box({.x = 4.0F, .y = 0.1F, .z = 2.0F}), .material = {.friction = friction}}); }))
    {
        return;
    }
    const Vector3 resting = Vector3{.x = 5.0F, .y = 2.0F} + rotate(tilt, Vector3{.y = 1.0F}) * 0.3F;
    const Body held = make_body(*made, {.pose = {.position = resting, .orientation = tilt}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F}), .material = {.friction = friction}});
    const Body pushed =
        make_body(*made, {.pose = {.position = resting + Vector3{.z = 1.0F}, .orientation = tilt}}, {.shape = box({.x = 0.2F, .y = 0.2F, .z = 0.2F}), .material = {.friction = friction}});
    advance(*made, 30);
    pushed.body->set_linear_velocity(rotate(tilt, Vector3{.x = -2.0F}));
    advance(*made, 60);
    EXPECT_LT(length(held.body->state().pose.position - resting), 0.03F) << "static friction holds";
    EXPECT_GT(length(pushed.body->state().pose.position - (resting + Vector3{.z = 1.0F})), 1.0F) << "dynamic friction lets it slide";
}

TEST_P(RigidConformanceTest, SimulationsAreDeterministicWhateverTheThreads)
{
    // 60 boxes and spheres thrown onto a floor, 120 steps: two runs, and a run with a scheduler of four threads, give the
    // same bits; every body on the floor.
    const auto thrown = [this](physics::TaskScheduler* scheduler) {
        const std::unique_ptr<PhysicsWorld> made = world({.task_scheduler = scheduler});
        const Body floor = make_floor(*made);
        std::vector<Body> bodies;
        for (int index = 0; index < 60; ++index)
        {
            const float x = static_cast<float>(index % 6) - 2.5F;
            const float z = static_cast<float>((index / 6) % 5) - 2.0F;
            bodies.push_back(make_body(*made,
                                       {.pose = {.position = {.x = x * 1.1F, .y = 1.0F + static_cast<float>(index / 30) * 1.2F, .z = z * 1.1F},
                                                 .orientation = Quaternion::from_axis_angle({.y = 1}, 0.1F * static_cast<float>(index))},
                                        .linear_velocity = {.x = 0.3F * static_cast<float>(index % 3 - 1)},
                                        .angular_velocity = {.y = 0.5F}},
                                       {.shape = index % 2 == 0 ? box({.x = 0.4F, .y = 0.4F, .z = 0.4F}) : sphere(0.45F)}));
        }
        advance(*made, 120);
        std::vector<Transform> poses;
        for (const Body& body : bodies)
        {
            poses.push_back(body.body->state().pose);
        }
        return poses;
    };
    const std::vector<Transform> first = thrown(nullptr);
    const std::vector<Transform> second = thrown(nullptr);
    ThreadTaskScheduler threads{4};
    const std::vector<Transform> threaded = thrown(&threads);
    ASSERT_EQ(first.size(), threaded.size());
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        EXPECT_EQ(bits(first[index].position), bits(second[index].position)) << index;
        EXPECT_EQ(bits(first[index].position), bits(threaded[index].position)) << index;
        EXPECT_EQ(first[index].orientation, threaded[index].orientation) << index;
        EXPECT_GT(first[index].position.y, 0.0F) << index << ": on the floor, not through it";
    }
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         RigidConformanceTest,
                         testing::ValuesIn(conformance_backends()),
                         conformance_test_name);

} // namespace
} // namespace gfx::tests::conformance
