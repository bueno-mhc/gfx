// The Box3D backend: simulations with known outcomes (free fall, a resting stack that falls asleep, a pendulum against its
// analytic period, hinge limits, a rope, a breaking weld, a kinematic platform), events (contacts begin, end and hit; a
// trigger entered, stayed in and left), determinism (two runs, and runs on one and on four threads: the same bits), every
// capability Box3D lacks refused when recorded, compounds as several shapes, static meshes and height fields, Box3D's own
// queries against the abstraction's, contacts of a body.
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
box3d_world(const PhysicsWorldDescription& description = {}) -> std::unique_ptr<PhysicsWorld>
{
    return create_physics_world(description, PhysicsBackendKind::BOX3D);
}

/// A static floor (a box whose top is at y = 0).
struct Floor
{
    std::unique_ptr<RigidBody> body;
    std::unique_ptr<Collider> collider;
};

[[nodiscard]] auto
make_floor(PhysicsWorld& world) -> Floor
{
    Floor floor{.body = world.create_rigid_body({.kind = BodyKind::STATIC}), .collider = nullptr};
    floor.collider = floor.body->add_collider({.shape = box({.x = 50, .y = 0.5F, .z = 50}), .pose = {.position = {.y = -0.5F}}});
    return floor;
}

/// @p operation throws the BackendCapabilityError of @p capability, naming Box3D.
template<class Operation>
void
expect_refused(Operation&& operation,
               std::string_view capability)
{
    try
    {
        operation();
        ADD_FAILURE() << "not refused (expected " << capability << ")";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_EQ(error.capability(), capability) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("[gfx::physics::BOX3D]"), std::string_view::npos) << error.what();
    }
}

TEST(Box3dTest, TheFactoryMakesBox3dWorlds)
{
    EXPECT_TRUE(physics_backend_availability(PhysicsBackendKind::BOX3D).available);
    EXPECT_EQ(available_physics_backends().front(), PhysicsBackendKind::BOX3D);
    const std::unique_ptr<PhysicsWorld> world = box3d_world();
    EXPECT_TRUE(world->backend() == PhysicsBackendKind::BOX3D);
    EXPECT_EQ(world->solver().kind(), SolverKind::SOFT_STEP);
    EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::BACKEND);
    std::vector<SpatialNode> nodes;
    EXPECT_THROW(world->broad_phase().structure_nodes(nodes), BackendCapabilityError) << "Box3D's own broad phase is not shown";
    EXPECT_THROW((void)box3d_world({.gravity = {.y = std::numeric_limits<float>::quiet_NaN()}}), PhysicsError);
}

TEST(Box3dTest, ABodyFallsFreely)
{
    // Semi-implicit Euler over 4 sub-steps: the velocity is exact, the drop off by a quarter of a sub-step per step.
    const std::unique_ptr<PhysicsWorld> world = box3d_world();
    const std::unique_ptr<RigidBody> ball = world->create_rigid_body({.pose = {.position = {.y = 100}}, .angular_damping = 0.0F});
    const std::unique_ptr<Collider> collider = ball->add_collider({.shape = sphere(0.5F)});
    for (int step = 0; step < 60; ++step)
    {
        world->step(STEP);
    }
    const RigidBodyState state = ball->state();
    EXPECT_NEAR(state.linear_velocity.y, -STANDARD_GRAVITY, 1e-3F);
    EXPECT_NEAR(100.0F - state.pose.position.y, 0.5F * STANDARD_GRAVITY, 0.01F * STANDARD_GRAVITY);
    EXPECT_NEAR(ball->mass_properties().mass, 4.0F / 3.0F * PI * 0.125F * 1000.0F, 1.0F) << "Box3D's mass, published";
}

TEST(Box3dTest, AStackComesToRestAndFallsAsleep)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world();
    const Floor floor = make_floor(*world);
    std::vector<std::unique_ptr<RigidBody>> boxes;
    std::vector<std::unique_ptr<Collider>> colliders;
    for (int level = 0; level < 5; ++level)
    {
        boxes.push_back(world->create_rigid_body({.pose = {.position = {.y = 0.5F + static_cast<float>(level) * 1.01F}}}));
        colliders.push_back(boxes.back()->add_collider({.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
    }
    bool fell_asleep = false;
    for (int step = 0; step < 300; ++step)
    {
        world->step(STEP);
        fell_asleep = fell_asleep || std::ranges::any_of(world->events().sleeps, [](const SleepEvent& event) { return event.kind == SleepEventKind::FELL_ASLEEP; });
    }
    for (std::size_t level = 0; level < boxes.size(); ++level)
    {
        const RigidBodyState state = boxes[level]->state();
        EXPECT_NEAR(state.pose.position.y, 0.5F + static_cast<float>(level), 0.03F) << "level " << level;
        EXPECT_NEAR(state.pose.position.x, 0.0F, 0.01F) << "level " << level;
        EXPECT_LT(length(state.linear_velocity), 0.05F);
    }
    EXPECT_TRUE(fell_asleep);
    EXPECT_FALSE(boxes.front()->state().awake);

    // The bottom box touches the floor and the box above.
    std::array<ContactInfo, 8> contacts{};
    const QueryResult<std::span<ContactInfo>> touching = world->contacts_of(boxes.front()->handle(), contacts);
    ASSERT_TRUE(touching.has_value());
    EXPECT_EQ(touching->size(), 2U);
    for (const ContactInfo& contact : *touching)
    {
        EXPECT_EQ(contact.collider, colliders.front()->handle());
        EXPECT_GE(contact.manifold.point_count, 3U);
    }
}

TEST(Box3dTest, APendulumSwingsWithItsPeriod)
{
    // A small heavy bob 1 m below a hinge to the world, released at 5 degrees: T = 2π √(L / g).
    const std::unique_ptr<PhysicsWorld> world = box3d_world({.solver = {.sub_steps = 8}});
    constexpr float LENGTH = 1.0F;
    constexpr float START = 5.0F * PI / 180.0F;
    const Vector3 bob_position{.x = LENGTH * std::sin(START), .y = 2.0F - LENGTH * std::cos(START)};
    const std::unique_ptr<RigidBody> bob = world->create_rigid_body({.pose = {.position = bob_position}, .angular_damping = 0.0F});
    const std::unique_ptr<Collider> collider = bob->add_collider({.shape = sphere(0.02F), .density = 20000.0F});
    const std::unique_ptr<Joint> hinge = world->create_joint(hinge_joint({.first = bob->handle(), .anchor = {.y = 2.0F}, .axis = {.z = 1.0F}}));

    // Zero crossings of x from + to - give the period.
    std::vector<float> crossings;
    float previous = bob_position.x;
    for (int step = 1; step <= 60 * 7; ++step)
    {
        world->step(STEP);
        const float x = bob->state().pose.position.x;
        if (previous > 0.0F && x <= 0.0F)
        {
            crossings.push_back(static_cast<float>(step) * STEP - STEP * x / (x - previous));
        }
        previous = x;
    }
    ASSERT_GE(crossings.size(), 3U);
    const float period = (crossings.back() - crossings.front()) / static_cast<float>(crossings.size() - 1);
    EXPECT_NEAR(period, 2.0F * PI * std::sqrt(LENGTH / STANDARD_GRAVITY), 0.02F * 2.0F * PI * std::sqrt(LENGTH / STANDARD_GRAVITY));
    EXPECT_NEAR(length(bob->state().pose.position - Vector3{.y = 2.0F}), LENGTH, 0.01F) << "the hinge holds the distance";
}

TEST(Box3dTest, JointsHoldTheirLimitsRopesTheirLengthAndWeldsBreak)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world();

    // An arm on a hinge limited to ±0.5 rad from horizontal falls to its lower limit.
    const std::unique_ptr<RigidBody> arm = world->create_rigid_body({.pose = {.position = {.x = 1.0F, .y = 5.0F}}});
    const std::unique_ptr<Collider> arm_collider = arm->add_collider({.shape = box({.x = 1.0F, .y = 0.05F, .z = 0.05F})});
    const std::unique_ptr<Joint> hinge = world->create_joint(
        hinge_joint({.first = arm->handle(), .anchor = {.y = 5.0F}, .axis = {.z = 1.0F}, .limits = JointLimits{.lower = -0.5F, .upper = 0.5F}}));

    // A ball on a rope of 2 m from a point 10 m up falls to the rope's end.
    const std::unique_ptr<RigidBody> ball = world->create_rigid_body({.pose = {.position = {.x = 5.0F, .y = 9.5F}}});
    const std::unique_ptr<Collider> ball_collider = ball->add_collider({.shape = sphere(0.1F)});
    const std::unique_ptr<Joint> rope = world->create_joint(
        distance_joint({.first = ball->handle(), .anchor_first = {}, .anchor_second = {.x = 5.0F, .y = 10.0F}, .length = 2.0F, .mode = DistanceMode::ROPE}));

    // A heavy block welded to the world with a weak weld breaks.
    const std::unique_ptr<RigidBody> block = world->create_rigid_body({.pose = {.position = {.x = -5.0F, .y = 5.0F}}});
    const std::unique_ptr<Collider> block_collider = block->add_collider({.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .density = 5000.0F});
    const std::unique_ptr<Joint> weld = world->create_joint(fixed_joint({.first = block->handle(), .anchor = {.x = -5.0F, .y = 5.0F}, .breaking = {.force = 1000.0F}}));

    bool broke = false;
    for (int step = 0; step < 180; ++step)
    {
        world->step(STEP);
        broke = broke || std::ranges::any_of(world->events().joints, [&weld](const JointEvent& event) { return event.joint == weld->handle(); });
    }
    const Vector3 tip = transform_point(arm->state().pose, Vector3{.x = 1.0F});
    EXPECT_NEAR(std::atan2(tip.y - 5.0F, tip.x), -0.5F, 0.05F) << "the lower limit";
    EXPECT_LE(length(ball->state().pose.position - Vector3{.x = 5.0F, .y = 10.0F}), 2.0F + 0.02F);
    EXPECT_GT(length(ball->state().pose.position - Vector3{.x = 5.0F, .y = 10.0F}), 1.9F);
    EXPECT_TRUE(broke);
    EXPECT_TRUE(weld->state().broken);
    EXPECT_LT(block->state().pose.position.y, 0.0F) << "it fell once free";
}

TEST(Box3dTest, ContactAndTriggerEventsAreReported)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world({.hit_event_speed = 1.0F});
    const Floor floor = make_floor(*world);
    const std::unique_ptr<RigidBody> ball = world->create_rigid_body({.pose = {.position = {.y = 3.0F}}});
    std::unique_ptr<Collider> collider = ball->add_collider({.shape = sphere(0.25F)});
    const ColliderHandle ball_handle = collider->handle();
    const std::unique_ptr<Trigger> zone = world->create_trigger({.shape = box({.x = 1.0F, .y = 0.5F, .z = 1.0F}), .pose = {.position = {.y = 1.5F}}});

    // The ball falls through the trigger onto the floor.
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

/// The poses of a small scene of boxes and spheres thrown onto a floor after 120 steps.
[[nodiscard]] auto
simulate_scene(TaskScheduler* scheduler) -> std::vector<Transform>
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world({.task_scheduler = scheduler});
    const Floor floor = make_floor(*world);
    std::vector<std::unique_ptr<RigidBody>> bodies;
    std::vector<std::unique_ptr<Collider>> colliders;
    for (int index = 0; index < 60; ++index)
    {
        const float x = static_cast<float>(index % 6) - 2.5F;
        const float z = static_cast<float>((index / 6) % 5) - 2.0F;
        bodies.push_back(world->create_rigid_body(
            {.pose = {.position = {.x = x * 1.1F, .y = 1.0F + static_cast<float>(index / 30) * 1.2F, .z = z * 1.1F}, .orientation = Quaternion::from_axis_angle({.y = 1}, 0.1F * static_cast<float>(index))},
             .linear_velocity = {.x = 0.3F * static_cast<float>(index % 3 - 1)},
             .angular_velocity = {.y = 0.5F}}));
        colliders.push_back(bodies.back()->add_collider({.shape = index % 2 == 0 ? box({.x = 0.4F, .y = 0.4F, .z = 0.4F}) : sphere(0.45F)}));
    }
    for (int step = 0; step < 120; ++step)
    {
        world->step(STEP);
    }
    std::vector<Transform> poses;
    for (const std::unique_ptr<RigidBody>& body : bodies)
    {
        poses.push_back(body->state().pose);
    }
    return poses;
}

TEST(Box3dTest, SimulationsAreDeterministicWhateverTheThreads)
{
    // Two runs, and a run whose step goes through Box3D's task hooks on four threads: the same bits.
    const std::vector<Transform> first = simulate_scene(nullptr);
    const std::vector<Transform> second = simulate_scene(nullptr);
    ThreadTaskScheduler threads{4};
    const std::vector<Transform> threaded = simulate_scene(&threads);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        EXPECT_EQ((std::bit_cast<std::array<std::uint32_t, 3>>(first[index].position)), (std::bit_cast<std::array<std::uint32_t, 3>>(second[index].position))) << index;
        EXPECT_EQ((std::bit_cast<std::array<std::uint32_t, 3>>(first[index].position)), (std::bit_cast<std::array<std::uint32_t, 3>>(threaded[index].position))) << index;
        EXPECT_EQ(first[index].orientation, threaded[index].orientation) << index;
    }
}

TEST(Box3dTest, WhatBox3dCannotDoIsRefusedWhenRecorded)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world();
    const std::unique_ptr<RigidBody> body = world->create_rigid_body({});
    const std::unique_ptr<RigidBody> ground = world->create_rigid_body({.kind = BodyKind::STATIC});

    expect_refused([&] { (void)world->create_joint(six_degrees_of_freedom_joint({.first = body->handle()})); }, "SIX_DEGREES_OF_FREEDOM_JOINTS");
    expect_refused([&] { (void)world->create_joint(hinge_joint({.first = body->handle(), .limits = JointLimits{.lower = -3.2F, .upper = 0.0F}})); }, "WIDE_JOINT_LIMITS");
    expect_refused([&] { (void)world->create_joint(cone_twist_joint({.first = body->handle(), .twist_limits = JointLimits{.lower = 0.0F, .upper = 3.14F}})); },
                   "WIDE_JOINT_LIMITS");
    expect_refused([&] { (void)body->add_collider({.shape = sphere(1), .material = {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.5F}}}); },
                   "SEPARATE_STATIC_FRICTION");
    expect_refused([&] { (void)body->add_collider({.shape = make_shape(BoxShape{.half_extents = {.x = 1, .y = 1, .z = 1}, .radius = 0.1F})}); }, "ROUNDED_HULLS");
    expect_refused([&] { (void)body->add_collider({.shape = make_shape(CylinderShape{}), .round_shape_segments = 80}); }, "LARGE_HULLS");
    std::vector<Vector3> points;
    for (int index = 0; index < 400; ++index)
    {
        const float height = 1.0F - 2.0F * (static_cast<float>(index) + 0.5F) / 400.0F;
        const float angle = 2.39996323F * static_cast<float>(index);
        const float ring = std::sqrt(1.0F - height * height);
        points.push_back({.x = ring * std::cos(angle), .y = height, .z = ring * std::sin(angle)});
    }
    expect_refused([&] { (void)body->add_collider({.shape = make_shape(HullShape::from_points(points))}); }, "LARGE_HULLS");

    // Meshes and height fields on static bodies only, height fields in their body's frame.
    const std::array<Vector3, 3> corners{Vector3{}, {.x = 1}, {.z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 1> triangle{{{0, 2, 1}}};
    const auto mesh = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = triangle}});
    expect_refused([&] { (void)body->add_collider({.shape = mesh}); }, "DYNAMIC_TRIANGLE_MESHES");
    const std::array<float, 4> heights{0, 0, 0, 0};
    const auto field = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 2, 2}}});
    expect_refused([&] { (void)body->add_collider({.shape = field}); }, "DYNAMIC_HEIGHT_FIELDS");
    expect_refused([&] { (void)ground->add_collider({.shape = field, .pose = {.position = {.y = 1}}}); }, "POSED_HEIGHT_FIELDS");
    const std::unique_ptr<Collider> terrain = ground->add_collider({.shape = mesh});
    expect_refused([&] { ground->set_kind(BodyKind::DYNAMIC); }, "DYNAMIC_TRIANGLE_MESHES");

    // User constraints: the reference backend's.
    class Nothing final : public UserConstraint
    {
    public:
        [[nodiscard]] auto row_count() const noexcept -> std::uint32_t override
        {
            return 1;
        }

        void rows(const ConstraintFrame& /*first*/,
                  const ConstraintFrame& /*second*/,
                  std::span<ConstraintRow> /*rows*/) const noexcept override
        {
        }
    };
    expect_refused([&] { (void)world->create_constraint({.first = body->handle(), .constraint = std::make_shared<Nothing>()}); }, "USER_CONSTRAINTS");
}

TEST(Box3dTest, CompoundsMeshesAndHeightFieldsCollide)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world();

    // A static height field and a static mesh (two-sided: a ball under it rises into it and stops).
    std::array<float, 25> heights{};
    const auto field = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 5, 5}, .scale = {.x = 4, .y = 1, .z = 4}}});
    const std::unique_ptr<RigidBody> terrain = world->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = -8, .z = -8}}});
    const std::unique_ptr<Collider> terrain_collider = terrain->add_collider({.shape = field});
    const std::array<Vector3, 4> corners{Vector3{.x = 20, .y = 5, .z = -1}, {.x = 22, .y = 5, .z = -1}, {.x = 22, .y = 5, .z = 1}, {.x = 20, .y = 5, .z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 2> quad{{{0, 2, 1}, {0, 3, 2}}};
    const std::unique_ptr<RigidBody> ceiling = world->create_rigid_body({.kind = BodyKind::STATIC});
    const std::unique_ptr<Collider> ceiling_collider = ceiling->add_collider({.shape = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = quad, .two_sided = true}})});

    // A dumbbell (a compound of two spheres) on the height field; a ball pushed up under the ceiling.
    const std::unique_ptr<RigidBody> dumbbell = world->create_rigid_body({.pose = {.position = {.y = 2}}});
    const std::unique_ptr<Collider> weights = dumbbell->add_collider({.shape = make_shape(CompoundShape{{CompoundChild{.shape = sphere(0.5F), .pose = {.position = {.x = -1}}},
                                                                                                      CompoundChild{.shape = sphere(0.5F), .pose = {.position = {.x = 1}}}}})});
    const std::unique_ptr<RigidBody> balloon = world->create_rigid_body({.pose = {.position = {.x = 21, .y = 3}}, .gravity_scale = -1.0F});
    const std::unique_ptr<Collider> balloon_collider = balloon->add_collider({.shape = sphere(0.5F)});
    for (int step = 0; step < 180; ++step)
    {
        world->step(STEP);
    }
    EXPECT_NEAR(dumbbell->state().pose.position.y, 0.5F, 0.02F);
    EXPECT_NEAR(dumbbell->mass_properties().mass, 2.0F * 4.0F / 3.0F * PI * 0.125F * 1000.0F, 2.0F);
    EXPECT_NEAR(balloon->state().pose.position.y, 4.5F, 0.02F) << "stopped by the mesh's back side";
}

TEST(Box3dTest, Box3dsQueriesAgreeWithTheAbstractions)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world({.gravity = {}});
    const Floor floor = make_floor(*world);
    std::vector<std::unique_ptr<RigidBody>> bodies;
    std::vector<std::unique_ptr<Collider>> colliders;
    const std::array<std::shared_ptr<const Shape>, 3> shapes{sphere(0.6F), box({.x = 0.5F, .y = 0.7F, .z = 0.4F}), make_shape(CapsuleShape{.half_height = 0.5F, .radius = 0.3F})};
    for (int index = 0; index < 12; ++index)
    {
        bodies.push_back(world->create_rigid_body({.kind = BodyKind::STATIC,
                                                   .pose = {.position = {.x = static_cast<float>(index % 4) * 3.0F - 4.5F, .y = 2.0F, .z = static_cast<float>(index / 4) * 3.0F - 3.0F},
                                                            .orientation = Quaternion::from_axis_angle(normalized(Vector3{.x = 1, .y = 2, .z = 3}), static_cast<float>(index))}}));
        colliders.push_back(bodies.back()->add_collider({.shape = shapes[static_cast<std::size_t>(index) % shapes.size()]}));
    }
    world->step(STEP);

    // Rays from above at every collider and between them: the same collider, distance and normal.
    const QueryOptions abstraction{.engine = QueryEngine::ABSTRACTION};
    int hits = 0;
    for (int index = 0; index < 200; ++index)
    {
        const Vector3 origin{.x = static_cast<float>(index % 20) * 0.6F - 6.0F, .y = 10.0F, .z = static_cast<float>(index / 20) * 0.8F - 4.0F};
        const Ray ray{.origin = origin, .direction = normalized(Vector3{.x = 0.1F, .y = -1.0F, .z = 0.05F})};
        const QueryResult<RayHit> backend_hit = world->ray_cast(ray, 50);
        const QueryResult<RayHit> abstraction_hit = world->ray_cast(ray, 50, abstraction);
        ASSERT_EQ(backend_hit.has_value(), abstraction_hit.has_value()) << index;
        if (backend_hit)
        {
            ++hits;
            EXPECT_EQ(backend_hit->collider, abstraction_hit->collider) << index;
            EXPECT_NEAR(backend_hit->distance, abstraction_hit->distance, 2e-3F) << index;
            EXPECT_GT(dot(backend_hit->normal, abstraction_hit->normal), 0.99F) << index;
        }
    }
    EXPECT_EQ(hits, 200) << "the floor is under every ray";

    // Every hit along a ray, a sphere cast and overlaps.
    const Ray along{.origin = {.x = -10.0F, .y = 2.0F, .z = -3.0F}, .direction = {.x = 1.0F}};
    std::array<RayHit, 8> backend_hits{};
    std::array<RayHit, 8> abstraction_hits{};
    const auto backend_all = world->ray_cast_all(along, 30, backend_hits);
    const auto abstraction_all = world->ray_cast_all(along, 30, abstraction_hits, abstraction);
    ASSERT_TRUE(backend_all && abstraction_all);
    ASSERT_EQ(backend_all->size(), abstraction_all->size());
    for (std::size_t index = 0; index < backend_all->size(); ++index)
    {
        EXPECT_EQ((*backend_all)[index].collider, (*abstraction_all)[index].collider);
    }
    const Shape probe{SphereShape{.radius = 0.25F}};
    const auto backend_cast = world->shape_cast(probe, {.position = {.x = -4.5F, .y = 8.0F, .z = -3.0F}}, {.y = -10.0F});
    const auto abstraction_cast = world->shape_cast(probe, {.position = {.x = -4.5F, .y = 8.0F, .z = -3.0F}}, {.y = -10.0F}, abstraction);
    ASSERT_TRUE(backend_cast && abstraction_cast);
    EXPECT_EQ(backend_cast->collider, abstraction_cast->collider);
    EXPECT_NEAR(backend_cast->fraction, abstraction_cast->fraction, 2e-3F);
    EXPECT_GT(dot(backend_cast->normal, abstraction_cast->normal), 0.99F);
    std::array<ColliderHandle, 16> backend_overlap{};
    std::array<ColliderHandle, 16> abstraction_overlap{};
    const Shape big{SphereShape{.radius = 2.5F}};
    const auto backend_found = world->overlap(big, {.position = {.x = -3.0F, .y = 2.0F, .z = 0.0F}}, backend_overlap);
    const auto abstraction_found = world->overlap(big, {.position = {.x = -3.0F, .y = 2.0F, .z = 0.0F}}, abstraction_overlap, abstraction);
    ASSERT_TRUE(backend_found && abstraction_found);
    EXPECT_EQ(std::vector(backend_found->begin(), backend_found->end()), std::vector(abstraction_found->begin(), abstraction_found->end()));
}

TEST(Box3dTest, KinematicBodiesReachTheirTargetsAndCarryOthers)
{
    const std::unique_ptr<PhysicsWorld> world = box3d_world();
    const std::unique_ptr<RigidBody> platform = world->create_rigid_body({.kind = BodyKind::KINEMATIC});
    const std::unique_ptr<Collider> deck = platform->add_collider({.shape = box({.x = 2.0F, .y = 0.1F, .z = 2.0F})});
    const std::unique_ptr<RigidBody> crate = world->create_rigid_body({.pose = {.position = {.y = 0.6F}}});
    const std::unique_ptr<Collider> crate_collider = crate->add_collider({.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
    for (int step = 1; step <= 120; ++step)
    {
        platform->move_kinematic({.position = {.y = static_cast<float>(step) * 0.02F}}, STEP);
        world->step(STEP);
    }
    EXPECT_NEAR(platform->state().pose.position.y, 2.4F, 1e-3F);
    EXPECT_NEAR(crate->state().pose.position.y, 2.4F + 0.6F, 0.03F) << "lifted with it";
}

TEST(Box3dTest, WorldsStepTogetherAndAnswerQueriesFromManyThreads)
{
    // Two worlds stepping asynchronously at the same time; between their steps, four threads cast rays into each through
    // Box3D's own queries. Each answer is a completed step's: the falling ball's top, at a height a step left it.
    struct Scene
    {
        std::unique_ptr<PhysicsWorld> world;
        Floor floor;
        std::unique_ptr<RigidBody> ball;
        std::unique_ptr<Collider> collider;
    };
    std::array<Scene, 2> scenes;
    for (Scene& scene : scenes)
    {
        scene.world = box3d_world({.asynchronous_stepping = true});
        scene.floor = make_floor(*scene.world);
        scene.ball = scene.world->create_rigid_body({.pose = {.position = {.y = 20.0F}}});
        scene.collider = scene.ball->add_collider({.shape = sphere(0.5F)});
        scene.world->step(STEP);
    }
    std::atomic<bool> done{false};
    std::atomic<int> answered{0};
    std::atomic<int> wrong{0};
    {
        std::vector<std::jthread> readers;
        for (int reader = 0; reader < 4; ++reader)
        {
            readers.emplace_back([&, reader] {
                const Scene& scene = scenes[static_cast<std::size_t>(reader % 2)];
                while (!done.load())
                {
                    const QueryResult<RayHit> hit = scene.world->ray_cast({.origin = {.y = 50.0F}, .direction = {.y = -1.0F}}, 100);
                    if (hit)
                    {
                        (hit->collider == scene.collider->handle() && hit->distance > 29.0F ? answered : wrong).fetch_add(1);
                    }
                    else if (hit.error().kind != QueryFailureKind::STEP_IN_PROGRESS)
                    {
                        wrong.fetch_add(1);
                    }
                }
            });
        }
        for (int step = 0; step < 60; ++step)
        {
            for (Scene& scene : scenes)
            {
                scene.world->begin_step(STEP);
            }
            for (Scene& scene : scenes)
            {
                scene.world->wait_step();
            }
        }
        done.store(true);
    }
    EXPECT_EQ(wrong.load(), 0);
    EXPECT_GT(answered.load(), 0);
    EXPECT_EQ(scenes[0].ball->state().pose.position, scenes[1].ball->state().pose.position) << "the same simulation in both";
}

} // namespace
