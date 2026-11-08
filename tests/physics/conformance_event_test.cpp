// The conformance suite, events, queries, threads and refusals: on every backend, a ball falling through a zone onto the
// floor begins a contact with a hit, enters, stays in and leaves the zone, and its destroyed collider ends the touch; a
// ball bouncing off a wall within one step still reports BEGIN, HIT and END; the backend's queries agree with the
// abstraction's; two worlds stepping asynchronously answer queries from four threads with completed steps only; a
// destroyed body takes its collider out of the simulation; the broad phase shows its structure (or refuses to, as a
// backend's own broad phase may); and every capability is either honored or refused when recorded, with a
// BackendCapabilityError naming it and the backend.
#include "conformance_test.hpp"

namespace gfx::tests::conformance
{
namespace
{

using EventConformanceTest = ConformanceTest;

TEST_P(EventConformanceTest, ContactAndTriggerEventsAreReported)
{
    const std::unique_ptr<PhysicsWorld> made = world({.hit_event_speed = 1.0F});
    const Body floor = make_floor(*made);
    const std::unique_ptr<RigidBody> ball = made->create_rigid_body({.pose = {.position = {.y = 3.0F}}});
    std::unique_ptr<Collider> collider = ball->add_collider({.shape = sphere(0.25F)});
    const ColliderHandle ball_handle = collider->handle();
    const std::unique_ptr<Trigger> zone = made->create_trigger({.shape = box({.x = 1.0F, .y = 0.5F, .z = 1.0F}), .pose = {.position = {.y = 1.5F}}});

    // The ball falls through the zone onto the floor.
    std::vector<ContactEvent> contacts;
    std::vector<TriggerEvent> triggers;
    for (int step = 0; step < 90; ++step)
    {
        made->step(STEP);
        contacts.insert(contacts.end(), made->events().contacts.begin(), made->events().contacts.end());
        triggers.insert(triggers.end(), made->events().triggers.begin(), made->events().triggers.end());
    }
    const auto count = [](const auto& events, auto kind) { return std::ranges::count_if(events, [kind](const auto& event) { return event.kind == kind; }); };
    EXPECT_EQ(count(contacts, ContactEventKind::BEGIN), 1);
    EXPECT_GE(count(contacts, ContactEventKind::HIT), 1);
    EXPECT_EQ(count(triggers, TriggerEventKind::ENTER), 1);
    EXPECT_EQ(count(triggers, TriggerEventKind::EXIT), 1);
    EXPECT_GE(count(triggers, TriggerEventKind::STAY), 1);
    const auto begin = std::ranges::find(contacts, ContactEventKind::BEGIN, &ContactEvent::kind);
    ASSERT_NE(begin, contacts.end());
    EXPECT_TRUE(between(*begin, floor.collider->handle(), ball_handle));
    const auto enter = std::ranges::find(triggers, TriggerEventKind::ENTER, &TriggerEvent::kind);
    ASSERT_NE(enter, triggers.end());
    EXPECT_EQ(enter->trigger, zone->handle());
    EXPECT_EQ(enter->collider, ball_handle);

    // Its contact from the ball's side; destroying the ball's collider ends the touch at the next step.
    const std::vector<ContactInfo> touching = contacts_of(*made, *ball);
    ASSERT_EQ(touching.size(), 1U);
    EXPECT_EQ(touching.front().collider, ball_handle);
    EXPECT_EQ(touching.front().other, floor.collider->handle());
    collider.reset();
    made->step(STEP);
    EXPECT_EQ(count(std::vector(made->events().contacts.begin(), made->events().contacts.end()), ContactEventKind::END), 1);
}

TEST_P(EventConformanceTest, HitsThatBounceOffWithinAStepAreReported)
{
    // A bouncy continuous ball at 60 m/s toward a wall, without gravity: stopped and returned within one step, it still
    // reports BEGIN, HIT and END with the wall.
    const std::unique_ptr<PhysicsWorld> made = world({.gravity = {}});
    const Body wall = make_body(*made, {.kind = BodyKind::STATIC}, {.shape = box({.x = 2.0F, .y = 2.0F, .z = 0.1F})});
    const Body ball = make_body(*made, {.pose = {.position = {.z = 2.7F}}, .linear_velocity = {.z = -60.0F}, .motion_quality = MotionQuality::CONTINUOUS},
                                {.shape = sphere(0.25F), .material = {.restitution = 1.0F}});
    std::array<bool, ENUMERATOR_COUNT<ContactEventKind>> seen{};
    for (int step = 0; step < 10; ++step)
    {
        made->step(STEP);
        for (const ContactEvent& event : made->events().contacts)
        {
            if (between(event, ball.collider->handle(), wall.collider->handle()))
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

TEST_P(EventConformanceTest, TheBackendsQueriesAgreeWithTheAbstractions)
{
    // Twelve turned spheres, boxes and capsules over a floor: rays from above give the abstraction's collider, distance and
    // normal; every hit along a ray, a sphere cast and an overlap alike.
    const std::unique_ptr<PhysicsWorld> made = world({.gravity = {}});
    const Body floor = make_floor(*made);
    std::vector<Body> bodies;
    const std::array<std::shared_ptr<const Shape>, 3> shapes{sphere(0.6F), box({.x = 0.5F, .y = 0.7F, .z = 0.4F}), make_shape(CapsuleShape{.half_height = 0.5F, .radius = 0.3F})};
    for (int index = 0; index < 12; ++index)
    {
        bodies.push_back(make_body(*made,
                                   {.kind = BodyKind::STATIC,
                                    .pose = {.position = {.x = static_cast<float>(index % 4) * 3.0F - 4.5F, .y = 2.0F, .z = static_cast<float>(index / 4) * 3.0F - 3.0F},
                                             .orientation = Quaternion::from_axis_angle(normalized(Vector3{.x = 1, .y = 2, .z = 3}), static_cast<float>(index))}},
                                   {.shape = shapes[static_cast<std::size_t>(index) % shapes.size()]}));
    }
    made->step(STEP);

    const QueryOptions abstraction{.engine = QueryEngine::ABSTRACTION};
    int hits = 0;
    for (int index = 0; index < 200; ++index)
    {
        const Vector3 origin{.x = static_cast<float>(index % 20) * 0.6F - 6.0F, .y = 10.0F, .z = static_cast<float>(index / 20) * 0.8F - 4.0F};
        const Ray ray{.origin = origin, .direction = normalized(Vector3{.x = 0.1F, .y = -1.0F, .z = 0.05F})};
        const QueryResult<RayHit> backend_hit = made->ray_cast(ray, 50);
        const QueryResult<RayHit> abstraction_hit = made->ray_cast(ray, 50, abstraction);
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

    const Ray along{.origin = {.x = -10.0F, .y = 2.0F, .z = -3.0F}, .direction = {.x = 1.0F}};
    std::array<RayHit, 8> backend_hits{};
    std::array<RayHit, 8> abstraction_hits{};
    const auto backend_all = made->ray_cast_all(along, 30, backend_hits);
    const auto abstraction_all = made->ray_cast_all(along, 30, abstraction_hits, abstraction);
    ASSERT_TRUE(backend_all && abstraction_all);
    ASSERT_EQ(backend_all->size(), abstraction_all->size());
    for (std::size_t index = 0; index < backend_all->size(); ++index)
    {
        EXPECT_EQ((*backend_all)[index].collider, (*abstraction_all)[index].collider);
    }
    const Shape probe{SphereShape{.radius = 0.25F}};
    const auto backend_cast = made->shape_cast(probe, {.position = {.x = -4.5F, .y = 8.0F, .z = -3.0F}}, {.y = -10.0F});
    const auto abstraction_cast = made->shape_cast(probe, {.position = {.x = -4.5F, .y = 8.0F, .z = -3.0F}}, {.y = -10.0F}, abstraction);
    ASSERT_TRUE(backend_cast && abstraction_cast);
    EXPECT_EQ(backend_cast->collider, abstraction_cast->collider);
    EXPECT_NEAR(backend_cast->fraction, abstraction_cast->fraction, 2e-3F);
    EXPECT_GT(dot(backend_cast->normal, abstraction_cast->normal), 0.99F);
    std::array<ColliderHandle, 16> backend_overlap{};
    std::array<ColliderHandle, 16> abstraction_overlap{};
    const Shape big{SphereShape{.radius = 2.5F}};
    const auto backend_found = made->overlap(big, {.position = {.x = -3.0F, .y = 2.0F, .z = 0.0F}}, backend_overlap);
    const auto abstraction_found = made->overlap(big, {.position = {.x = -3.0F, .y = 2.0F, .z = 0.0F}}, abstraction_overlap, abstraction);
    ASSERT_TRUE(backend_found && abstraction_found);
    EXPECT_EQ(std::vector(backend_found->begin(), backend_found->end()), std::vector(abstraction_found->begin(), abstraction_found->end()));
}

TEST_P(EventConformanceTest, WorldsStepTogetherAndAnswerQueriesFromManyThreads)
{
    // Two worlds stepping asynchronously at the same time; four threads cast rays into them meanwhile. Each answer is a
    // completed step's (the falling ball's top, at a height a step left it) or STEP_IN_PROGRESS; both worlds simulate
    // the same.
    struct Scene
    {
        std::unique_ptr<PhysicsWorld> world;
        Body floor;
        Body ball;
    };
    std::array<Scene, 2> scenes;
    for (Scene& scene : scenes)
    {
        scene.world = world({.asynchronous_stepping = true});
        scene.floor = make_floor(*scene.world);
        scene.ball = make_body(*scene.world, {.pose = {.position = {.y = 20.0F}}}, {.shape = sphere(0.5F)});
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
                        (hit->collider == scene.ball.collider->handle() && hit->distance > 29.0F ? answered : wrong).fetch_add(1);
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
    EXPECT_EQ(bits(scenes[0].ball.body->state().pose.position), bits(scenes[1].ball.body->state().pose.position)) << "the same simulation in both";
}

TEST_P(EventConformanceTest, ADestroyedBodyTakesItsCollidersOutOfTheSimulation)
{
    // A ball resting on a static shelf: the shelf's body destroyed (its collider goes with it), the ball falls to the floor
    // below and touches only the floor.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body floor = make_floor(*made);
    Body shelf = make_body(*made, {.kind = BodyKind::STATIC, .pose = {.position = {.y = 2.0F}}}, {.shape = box({.x = 1.0F, .y = 0.1F, .z = 1.0F})});
    const Body ball = make_body(*made, {.pose = {.position = {.y = 2.4F}}}, {.shape = sphere(0.3F)});
    advance(*made, 60);
    EXPECT_NEAR(ball.body->state().pose.position.y, 2.4F, 0.02F) << "on the shelf";
    shelf.collider.reset();
    shelf.body.reset();
    advance(*made, 90);
    EXPECT_NEAR(ball.body->state().pose.position.y, 0.3F, 0.02F) << "on the floor";
    const std::vector<ContactInfo> touching = contacts_of(*made, *ball.body);
    ASSERT_EQ(touching.size(), 1U);
    EXPECT_EQ(touching.front().other, floor.collider->handle());
}

TEST_P(EventConformanceTest, TheBroadPhaseShowsItsStructure)
{
    // Where the backend shows it (a debug view draws it): 20 balls resting on a floor, between steps; every collider is
    // listed in the nodes (once per leaf or cell that holds it), and its center lies in a node listing colliders.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body floor = make_floor(*made);
    std::vector<Body> balls;
    for (int index = 0; index < 20; ++index)
    {
        balls.push_back(make_body(*made, {.pose = {.position = {.x = static_cast<float>(index % 5) * 1.5F - 3.0F, .y = 0.5F, .z = static_cast<float>(index / 5) * 1.5F}}},
                                  {.shape = sphere(0.5F)}));
    }
    advance(*made, 10);
    std::vector<SpatialNode> nodes;
    if (backend() == PhysicsBackendKind::BOX3D)
    {
        // Box3D's own broad phase is not shown (the GPU's own is: its trees read back).
        EXPECT_THROW(made->broad_phase().structure_nodes(nodes), BackendCapabilityError);
        return;
    }
    made->broad_phase().structure_nodes(nodes);
    const std::uint32_t listed = std::ranges::fold_left(nodes, 0U, [](std::uint32_t sum, const SpatialNode& node) { return sum + node.proxies; });
    EXPECT_GE(listed, 21U) << "the floor and the balls";
    for (std::size_t index = 0; index < balls.size(); ++index)
    {
        const Vector3 center = balls[index].body->state().pose.position;
        EXPECT_TRUE(std::ranges::any_of(nodes, [&center](const SpatialNode& node) { return node.proxies > 0 && node.box.contains(center); })) << "ball " << index;
    }

    // Asked again, the same nodes.
    std::vector<SpatialNode> again;
    made->broad_phase().structure_nodes(again);
    EXPECT_EQ(again.size(), nodes.size());
}

TEST_P(EventConformanceTest, EveryCapabilityIsHonoredOrRefusedWhenRecorded)
{
    // Each operation needs one capability: the backend either does it, or refuses it at once with a BackendCapabilityError
    // naming the capability and itself (Box3D's limits, the GPU's user constraints, the pipelines of Box3D and the GPU).
    const std::unique_ptr<PhysicsWorld> made = world();
    const std::unique_ptr<RigidBody> body = made->create_rigid_body({});
    const std::unique_ptr<RigidBody> ground = made->create_rigid_body({.kind = BodyKind::STATIC});
    std::vector<std::unique_ptr<Collider>> colliders;
    std::vector<std::unique_ptr<Joint>> joints;
    const auto collider = [&](RigidBody& owner, const ColliderDescription& description) { colliders.push_back(owner.add_collider(description)); };

    (void)supports(PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS, [&] { joints.push_back(made->create_joint(six_degrees_of_freedom_joint({.first = body->handle()}))); });
    (void)supports(PhysicsCapability::WIDE_JOINT_LIMITS,
                   [&] { joints.push_back(made->create_joint(hinge_joint({.first = body->handle(), .limits = JointLimits{.lower = -3.2F, .upper = 0.0F}}))); });
    (void)supports(PhysicsCapability::WIDE_JOINT_LIMITS,
                   [&] { joints.push_back(made->create_joint(cone_twist_joint({.first = body->handle(), .twist_limits = JointLimits{.lower = 0.0F, .upper = 3.14F}}))); });
    (void)supports(PhysicsCapability::SEPARATE_STATIC_FRICTION,
                   [&] { collider(*body, {.shape = sphere(1), .material = {.friction = {.static_coefficient = 0.8F, .dynamic_coefficient = 0.5F}}}); });
    (void)supports(PhysicsCapability::ROUNDED_HULLS, [&] { collider(*body, {.shape = make_shape(BoxShape{.half_extents = {.x = 1, .y = 1, .z = 1}, .radius = 0.1F})}); });
    (void)supports(PhysicsCapability::LARGE_HULLS, [&] { collider(*body, {.shape = make_shape(CylinderShape{}), .round_shape_segments = 80}); });
    std::vector<Vector3> points;
    for (int index = 0; index < 400; ++index)
    {
        const float height = 1.0F - 2.0F * (static_cast<float>(index) + 0.5F) / 400.0F;
        const float angle = 2.39996323F * static_cast<float>(index);
        const float ring = std::sqrt(1.0F - height * height);
        points.push_back({.x = ring * std::cos(angle), .y = height, .z = ring * std::sin(angle)});
    }
    (void)supports(PhysicsCapability::LARGE_HULLS, [&] { collider(*body, {.shape = make_shape(HullShape::from_points(points))}); });

    // Meshes and height fields on moving bodies; height fields posed on their body.
    const std::array<Vector3, 3> corners{Vector3{}, {.x = 1}, {.z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 1> triangle{{{0, 2, 1}}};
    const auto mesh = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = triangle}});
    (void)supports(PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, [&] { collider(*body, {.shape = mesh}); });
    const std::array<float, 4> heights{0, 0, 0, 0};
    const auto field = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 2, 2}}});
    (void)supports(PhysicsCapability::DYNAMIC_HEIGHT_FIELDS, [&] { collider(*body, {.shape = field}); });
    (void)supports(PhysicsCapability::POSED_HEIGHT_FIELDS, [&] { collider(*ground, {.shape = field, .pose = {.position = {.y = 1}}}); });
    const std::unique_ptr<RigidBody> terrain = made->create_rigid_body({.kind = BodyKind::STATIC});
    collider(*terrain, {.shape = mesh});
    (void)supports(PhysicsCapability::DYNAMIC_TRIANGLE_MESHES, [&] { terrain->set_kind(BodyKind::DYNAMIC); });

    // User constraints; the pipeline's components.
    class Nothing final : public UserConstraint
    {
    public:
        [[nodiscard]] auto row_count() const noexcept -> std::uint32_t override
        {
            return 1;
        }

        void rows(const ConstraintFrame& /*first*/,
                  const ConstraintFrame& /*second*/,
                  std::span<ConstraintRow> rows) const noexcept override
        {
            rows[0] = {};
        }
    };
    std::unique_ptr<Constraint> constraint;
    (void)supports(PhysicsCapability::USER_CONSTRAINTS, [&] { constraint = made->create_constraint({.first = body->handle(), .constraint = std::make_shared<Nothing>()}); });
    (void)supports(PhysicsCapability::CUSTOM_BROAD_PHASE, [&] { made->replace_broad_phase(std::make_unique<SpatialBroadPhaseComponent<Bvh>>()); });
    (void)supports(PhysicsCapability::CUSTOM_NARROW_PHASE, [&] { made->replace_narrow_phase(std::make_unique<GjkNarrowPhase>()); });
    (void)supports(PhysicsCapability::CUSTOM_SOLVER, [&] { made->replace_solver(std::make_unique<SequentialImpulseSolver>()); });
    (void)supports(PhysicsCapability::CUSTOM_INTEGRATOR, [&] { made->replace_integrator(make_integrator(IntegratorKind::SEMI_IMPLICIT_EULER)); });

    // What was accepted simulates.
    advance(*made, 5);
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         EventConformanceTest,
                         testing::ValuesIn(conformance_backends()),
                         conformance_test_name);

} // namespace
} // namespace gfx::tests::conformance
