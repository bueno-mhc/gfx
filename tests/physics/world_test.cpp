// Worlds: the factory and the null backend's error contract; SteppedWorld on a recording backend — edits applied in the
// order recorded at the start of the next step (from several threads too), getters showing the last completed step,
// deferred and cascading destruction, objects of a destroyed world, asynchronous steps, queries refused during steps and
// answered between them from many threads (the reader handshake), the abstraction's queries, capability checks when
// edits are recorded, and descriptive errors for invalid values.
#include <gtest/gtest.h>

import std;
import gfx.physics;
import gfx.tests.physics_world_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::AppliedEdit;
using gfx::tests::edit_kind;
using gfx::tests::make_recording_world;

constexpr float STEP = 1.0F / 60.0F;

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

/// @p operation throws a PhysicsError whose message contains @p text.
template<class Operation>
void
expect_physics_error(Operation&& operation,
                     std::string_view text)
{
    try
    {
        operation();
        ADD_FAILURE() << "no PhysicsError (expected one saying '" << text << "')";
    }
    catch (const PhysicsError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find(text), std::string_view::npos) << error.what();
    }
}

/// @p operation throws the BackendCapabilityError of @p capability.
template<class Operation>
void
expect_capability_error(Operation&& operation,
                        std::string_view capability)
{
    try
    {
        operation();
        ADD_FAILURE() << "no BackendCapabilityError (expected " << capability << ")";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_EQ(error.capability(), capability) << error.what();
    }
}

// ===================================================================================================== factory, null backend

TEST(PhysicsFactoryTest, EachBackendReportsWhetherItWasBuiltAndCanRun)
{
    // Box3D when GFX_PHYSICS_BACKEND_BOX3D is on; the reference and null backends always; no custom backend in this build.
    const PhysicsBackendAvailability box3d = physics_backend_availability(PhysicsBackendKind::BOX3D);
    EXPECT_EQ(box3d.built, box3d.available);
    EXPECT_EQ(box3d.reason.empty(), box3d.available);
    EXPECT_TRUE(physics_backend_availability(PhysicsBackendKind::REFERENCE).available);
    EXPECT_TRUE(physics_backend_availability(PhysicsBackendKind::REFERENCE).reason.empty());
    EXPECT_FALSE(physics_backend_availability(PhysicsBackendKind::CUSTOM).built);
    EXPECT_NE(physics_backend_availability(PhysicsBackendKind::CUSTOM).reason.find("GFX_PHYSICS_CUSTOM_BACKEND_TARGET"), std::string::npos);
    EXPECT_TRUE(physics_backend_availability(PhysicsBackendKind::NULL_BACKEND).available);
    std::vector<PhysicsBackendKind> expected;
    if (box3d.available)
    {
        expected.push_back(PhysicsBackendKind::BOX3D);
    }
    expected.push_back(PhysicsBackendKind::REFERENCE);
    expected.push_back(PhysicsBackendKind::NULL_BACKEND);
    EXPECT_EQ(available_physics_backends(), expected) << "simulating backends first";
    EXPECT_TRUE(create_physics_world({}, PhysicsBackendKind::REFERENCE)->backend() == PhysicsBackendKind::REFERENCE);
    try
    {
        (void)create_physics_world({}, PhysicsBackendKind::CUSTOM);
        ADD_FAILURE() << "a custom world without a custom backend";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("[gfx::physics::CUSTOM]"), std::string_view::npos) << error.what();
    }
}

TEST(NullBackendTest, EveryOperationThrowsAndEveryQueryIsUnsupported)
{
    const std::unique_ptr<PhysicsWorld> world = create_physics_world({.gravity = {.y = -3}}, PhysicsBackendKind::NULL_BACKEND);
    EXPECT_TRUE(world->backend() == PhysicsBackendKind::NULL_BACKEND);
    EXPECT_EQ(world->description().gravity.y, -3.0F);
    EXPECT_FALSE(world->is_stepping());
    EXPECT_EQ(world->step_count(), 0U);
    EXPECT_TRUE(world->events().contacts.empty());
    EXPECT_EQ(world->statistics().step, 0U);
    EXPECT_NO_THROW(world->wait_step());

    // What would make or change something.
    constexpr std::string_view CAPABILITY = "WORLD_OBJECTS";
    expect_capability_error([&] { (void)world->create_rigid_body({}); }, CAPABILITY);
    expect_capability_error([&] { (void)world->create_soft_body({}); }, CAPABILITY);
    expect_capability_error([&] { (void)world->create_joint(HingeJointDescription{}); }, CAPABILITY);
    expect_capability_error([&] { (void)world->create_constraint({}); }, CAPABILITY);
    expect_capability_error([&] { (void)world->create_trigger({}); }, CAPABILITY);
    expect_capability_error([&] { world->set_gravity({}); }, CAPABILITY);
    expect_capability_error([&] { world->set_solver_description({}); }, CAPABILITY);
    expect_capability_error([&] { world->set_sleep_description({}); }, CAPABILITY);
    expect_capability_error([&] { world->step(STEP); }, CAPABILITY);
    expect_capability_error([&] { world->begin_step(STEP); }, CAPABILITY);
    expect_capability_error([&] { (void)world->broad_phase(); }, CAPABILITY);
    expect_capability_error([&] { (void)world->narrow_phase(); }, CAPABILITY);
    expect_capability_error([&] { (void)world->solver(); }, CAPABILITY);
    expect_capability_error([&] { (void)world->integrator(); }, CAPABILITY);
    expect_capability_error([&] { world->replace_broad_phase(nullptr); }, CAPABILITY);
    expect_capability_error([&] { world->replace_narrow_phase(nullptr); }, CAPABILITY);
    expect_capability_error([&] { world->replace_solver(nullptr); }, CAPABILITY);
    expect_capability_error([&] { world->replace_integrator(nullptr); }, CAPABILITY);
    expect_capability_error([&] { (void)world->soft_body_integrator(); }, CAPABILITY);
    expect_capability_error([&] { world->replace_soft_body_integrator(nullptr); }, CAPABILITY);

    // Queries never throw: they fail as UNSUPPORTED, which raise() turns into the capability error.
    const Shape ball{SphereShape{}};
    std::array<RayHit, 4> hits{};
    std::array<ColliderHandle, 4> colliders{};
    std::array<ContactInfo, 4> contacts{};
    const std::array<QueryFailure, 7> failures{world->ray_cast({}, 10).error(),
                                               world->ray_cast_all({}, 10, hits).error(),
                                               world->shape_cast(ball, {}, {.x = 1}).error(),
                                               world->overlap(ball, {}, colliders).error(),
                                               world->overlap(Aabb{}, colliders).error(),
                                               world->closest_point({}, 10).error(),
                                               world->contacts_of({}, contacts).error()};
    for (const QueryFailure& failure : failures)
    {
        EXPECT_EQ(failure.kind, QueryFailureKind::UNSUPPORTED) << failure.message();
        EXPECT_EQ(failure.capability, PhysicsCapability::WORLD_OBJECTS);
        EXPECT_THROW(failure.raise(), BackendCapabilityError);
    }
}

// ===================================================================================================== deferred edits

TEST(SteppedWorldTest, EditsAreAppliedInOrderAtTheStartOfTheNextStep)
{
    auto [world, backend] = make_recording_world({.gravity = {}});

    // Recorded, not applied: the getters show what the body was created with.
    const std::unique_ptr<RigidBody> body = world->create_rigid_body({.pose = {.position = {.y = 1}}});
    const std::unique_ptr<Collider> collider = body->add_collider({.shape = sphere(0.5F), .density = 2.0F});
    body->set_pose({.position = {.x = 2, .y = 1}});
    body->set_linear_velocity({.x = 6});
    body->set_damping(0.5F, 0.25F);
    collider->set_density(3.0F);
    EXPECT_TRUE(backend->applied().empty());
    EXPECT_EQ(body->state().pose.position, (Vector3{.y = 1}));
    EXPECT_EQ(body->description().linear_damping, 0.0F);
    EXPECT_EQ(collider->description().density, 2.0F);

    // One step: the edits in order, then the simulation; the getters show the step's results.
    world->step(STEP);
    const std::vector<AppliedEdit> applied = backend->applied();
    const std::vector<std::size_t> expected{edit_kind<edits::CreateRigidBody>(), edit_kind<edits::CreateCollider>(), edit_kind<edits::SetBodyPose>(),
                                            edit_kind<edits::SetBodyLinearVelocity>(), edit_kind<edits::SetBodyDamping>(), edit_kind<edits::SetColliderDensity>()};
    ASSERT_EQ(applied.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(applied[index].kind, expected[index]) << "edit " << index;
        EXPECT_EQ(applied[index].step, 1U);
    }
    EXPECT_EQ(applied[0].target, std::format("{}", body->handle()));
    EXPECT_EQ(applied[1].target, std::format("{}", collider->handle()));
    const float velocity = 6.0F / (1.0F + STEP * 0.5F);
    EXPECT_FLOAT_EQ(body->state().linear_velocity.x, velocity);
    EXPECT_FLOAT_EQ(body->state().pose.position.x, 2.0F + velocity * STEP);
    EXPECT_EQ(body->description().angular_damping, 0.25F);
    EXPECT_EQ(collider->description().density, 3.0F);
    EXPECT_EQ(world->step_count(), 1U);
    EXPECT_EQ(world->statistics().edits, 6U);
    EXPECT_EQ(world->statistics().rigid_bodies, 1U);

    // The mass of the dynamic body comes from its collider: a sphere of radius 0.5 at density 3.
    EXPECT_NEAR(body->mass_properties().mass, 4.0F / 3.0F * std::numbers::pi_v<float> * 0.125F * 3.0F, 1e-4F);
    EXPECT_NEAR(collider->world_bounds().minimum.x, body->state().pose.position.x - 0.5F, 1e-5F);

    // Edits recorded later wait for the next step.
    body->set_pose({.position = {.x = -4}});
    EXPECT_FLOAT_EQ(body->state().pose.position.x, 2.0F + velocity * STEP);
    world->step(STEP);
    EXPECT_NEAR(body->state().pose.position.x, -4.0F + body->state().linear_velocity.x * STEP, 1e-5F);
    EXPECT_EQ(backend->applied().back().step, 2U);
}

TEST(SteppedWorldTest, EditsFromManyThreadsAreAllAppliedEachThreadInItsOrder)
{
    auto [world, backend] = make_recording_world({.gravity = {}});
    constexpr int THREADS = 6;
    constexpr int EDITS = 200;
    std::vector<std::unique_ptr<RigidBody>> bodies;
    for (int thread = 0; thread < THREADS; ++thread)
    {
        bodies.push_back(world->create_rigid_body({.kind = BodyKind::KINEMATIC}));
    }
    world->step(STEP);

    // Each thread sets its body's velocity to 1, 2, 3, ... while the main thread keeps stepping.
    std::atomic<int> finished{0};
    {
        std::vector<std::jthread> threads;
        for (int thread = 0; thread < THREADS; ++thread)
        {
            threads.emplace_back([&, thread] {
                for (int edit = 1; edit <= EDITS; ++edit)
                {
                    bodies[static_cast<std::size_t>(thread)]->set_linear_velocity({.x = static_cast<float>(edit)});
                }
                finished.fetch_add(1);
            });
        }
        while (finished.load() < THREADS)
        {
            world->step(STEP);
        }
    }
    world->step(STEP);

    // Every edit applied, each body's values in the order its thread recorded them (1, 2, 3, ...).
    std::map<std::string, std::vector<float>> values_by_body;
    for (const AppliedEdit& edit : backend->applied())
    {
        if (edit.kind == edit_kind<edits::SetBodyLinearVelocity>())
        {
            values_by_body[edit.target].push_back(edit.value);
        }
    }
    ASSERT_EQ(values_by_body.size(), static_cast<std::size_t>(THREADS));
    for (const auto& [body, values] : values_by_body)
    {
        ASSERT_EQ(values.size(), static_cast<std::size_t>(EDITS)) << body;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            ASSERT_EQ(values[index], static_cast<float>(index + 1)) << body;
        }
    }
    for (const std::unique_ptr<RigidBody>& body : bodies)
    {
        EXPECT_EQ(body->state().linear_velocity.x, static_cast<float>(EDITS)) << "the last value recorded wins";
    }
}

// ===================================================================================================== destruction

TEST(SteppedWorldTest, DestructionIsDeferredAndABodyTakesItsAttachmentsWithIt)
{
    auto [world, backend] = make_recording_world();
    std::unique_ptr<RigidBody> anchor = world->create_rigid_body({.kind = BodyKind::STATIC});
    std::unique_ptr<RigidBody> body = world->create_rigid_body({.pose = {.position = {.y = 3}}});
    std::unique_ptr<Collider> collider = body->add_collider({.shape = sphere(0.5F)});
    std::unique_ptr<Trigger> trigger = world->create_trigger({.shape = sphere(1), .body = body->handle()});
    std::unique_ptr<Joint> joint = world->create_joint(BallAndSocketJointDescription{.first = body->handle(), .second = anchor->handle()});
    world->step(STEP);
    const RigidBodyHandle body_handle = body->handle();
    const std::size_t before = backend->applied().size();

    // Destroying the body: its collider, trigger and joint go first, all at the next step.
    body.reset();
    EXPECT_EQ(backend->applied().size(), before);
    expect_physics_error([&] { collider->set_density(1); }, "was destroyed with its body");
    expect_physics_error([&] { (void)joint->state(); }, "was destroyed with its body");
    expect_physics_error([&] { trigger->set_pose({}); }, "was destroyed");
    expect_physics_error([&] { (void)world->create_joint(HingeJointDescription{.first = body_handle}); }, "is not a live rigid body");
    world->step(STEP);
    const std::vector<AppliedEdit> applied = backend->applied();
    ASSERT_EQ(applied.size(), before + 4);
    EXPECT_EQ(applied[before].kind, edit_kind<edits::DestroyCollider>());
    EXPECT_EQ(applied[before + 1].kind, edit_kind<edits::DestroyTrigger>());
    EXPECT_EQ(applied[before + 2].kind, edit_kind<edits::DestroyJoint>());
    EXPECT_EQ(applied[before + 3].kind, edit_kind<edits::DestroyRigidBody>());
    EXPECT_EQ(world->statistics().rigid_bodies, 1U);

    // Destroying what was destroyed with the body records nothing more; the slot is reused with a new generation.
    collider.reset();
    trigger.reset();
    joint.reset();
    world->step(STEP);
    EXPECT_EQ(backend->applied().size(), before + 4);
    const std::unique_ptr<RigidBody> replacement = world->create_rigid_body({});
    EXPECT_NE(replacement->handle(), body_handle);
    EXPECT_EQ(std::format("{}", replacement->handle()).substr(0, 18), std::format("{}", body_handle).substr(0, 18)) << "the same slot";
}

TEST(SteppedWorldTest, ObjectsOfADestroyedWorldThrow)
{
    auto [world, backend] = make_recording_world();
    const std::unique_ptr<RigidBody> body = world->create_rigid_body({});
    const std::unique_ptr<Collider> collider = body->add_collider({.shape = sphere(1)});
    world->step(STEP);
    world.reset();
    expect_physics_error([&] { body->set_pose({}); }, "the world was destroyed");
    expect_physics_error([&] { (void)body->state(); }, "the world was destroyed");
    expect_physics_error([&] { (void)collider->description(); }, "the world was destroyed");
    expect_physics_error([&] { (void)body->add_collider({.shape = sphere(1)}); }, "the world was destroyed");
}

// ===================================================================================================== stepping

TEST(SteppedWorldTest, AsynchronousStepsRunOnTheWorldsThreadAndRefuseQueries)
{
    auto [world, backend] = make_recording_world({.gravity = {.y = -10}, .asynchronous_stepping = true});
    const std::unique_ptr<RigidBody> ground = world->create_rigid_body({.kind = BodyKind::STATIC});
    const std::unique_ptr<Collider> floor = ground->add_collider({.shape = box({.x = 10, .y = 1, .z = 10})});
    const std::unique_ptr<RigidBody> ball = world->create_rigid_body({.pose = {.position = {.y = 5}}});
    const std::unique_ptr<Collider> ball_collider = ball->add_collider({.shape = sphere(0.5F)});
    world->step(STEP);
    const Ray down{.origin = {.y = 20}, .direction = {.y = -1}};
    ASSERT_TRUE(world->ray_cast(down, 100).has_value());

    // A held step: begin_step returns at once; queries fail without blocking, getters still answer.
    backend->hold();
    world->begin_step(STEP);
    backend->wait_until_held();
    EXPECT_TRUE(world->is_stepping());
    std::vector<std::jthread> readers;
    std::atomic<int> refused{0};
    for (int reader = 0; reader < 4; ++reader)
    {
        readers.emplace_back([&] {
            const QueryResult<RayHit> hit = world->ray_cast(down, 100);
            refused.fetch_add(!hit && hit.error().kind == QueryFailureKind::STEP_IN_PROGRESS ? 1 : 0);
        });
    }
    readers.clear();
    EXPECT_EQ(refused.load(), 4);
    EXPECT_FLOAT_EQ(ball->state().pose.position.y, 5.0F + (-10.0F * STEP) * STEP) << "the last completed step";
    expect_physics_error([&] { world->begin_step(STEP); }, "a step is already running");

    // Released and waited for: published.
    backend->release();
    world->wait_step();
    EXPECT_FALSE(world->is_stepping());
    EXPECT_EQ(world->step_count(), 2U);
    EXPECT_FLOAT_EQ(ball->state().pose.position.y, 5.0F + (-10.0F * STEP) * STEP + (-20.0F * STEP) * STEP);
    const QueryResult<RayHit> hit = world->ray_cast(down, 100);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->collider, ball_collider->handle());
    EXPECT_EQ(hit->body, ball->handle());

    // A step that fails: wait_step() throws it, nothing is published, and the next step works.
    backend->fail_next_step();
    world->begin_step(STEP);
    EXPECT_THROW(world->wait_step(), std::runtime_error);
    EXPECT_EQ(world->step_count(), 2U);
    EXPECT_NO_THROW(world->step(STEP));
    EXPECT_EQ(world->step_count(), 3U);
    expect_physics_error([&] { world->step(0); }, "not finite and positive");
    expect_physics_error([&] { world->step(std::numeric_limits<float>::infinity()); }, "not finite and positive");
}

TEST(SteppedWorldTest, QueriesFromManyThreadsSeeOnlyCompletedSteps)
{
    // A ball teleported to x = k before step k; readers cast rays at it while steps run on the world's thread.
    auto [world, backend] = make_recording_world({.gravity = {}, .asynchronous_stepping = true});
    const std::unique_ptr<RigidBody> ball = world->create_rigid_body({.kind = BodyKind::STATIC});
    const std::unique_ptr<Collider> collider = ball->add_collider({.shape = sphere(0.25F)});
    world->step(STEP);
    std::atomic<bool> done{false};
    std::atomic<int> answered{0};
    std::atomic<int> refused{0};
    std::atomic<int> torn{0};
    constexpr int STEPS = 200;
    {
        std::vector<std::jthread> readers;
        for (int reader = 0; reader < 4; ++reader)
        {
            readers.emplace_back([&] {
                while (!done.load())
                {
                    const QueryResult<RayHit> hit = world->ray_cast({.origin = {.x = 1000}, .direction = {.x = -1}}, 2000);
                    if (!hit)
                    {
                        // Refused during a step; never a miss (the ball is always somewhere on the ray).
                        (hit.error().kind == QueryFailureKind::STEP_IN_PROGRESS ? refused : torn).fetch_add(1);
                        continue;
                    }

                    // The ball of a completed step: at an integer x (the published tree and records agree).
                    const float x = 1000.0F - hit->distance - 0.25F;
                    (std::abs(x - std::round(x)) < 1e-3F && x >= 0.0F && x <= STEPS ? answered : torn).fetch_add(1);
                }
            });
        }
        for (int step = 1; step <= STEPS; ++step)
        {
            ball->set_pose({.position = {.x = static_cast<float>(step)}});
            world->begin_step(STEP);
            world->wait_step();
        }
        done.store(true);
    }
    EXPECT_EQ(torn.load(), 0);
    EXPECT_GT(answered.load(), 0);
    EXPECT_NEAR(world->ray_cast({.origin = {.x = 1000}, .direction = {.x = -1}}, 2000)->distance, 1000.0F - STEPS - 0.25F, 1e-3F);
}

// ===================================================================================================== queries

TEST(SteppedWorldTest, TheAbstractionAnswersQueriesAboutThePublishedColliders)
{
    auto [world, backend] = make_recording_world({.gravity = {}});
    const std::unique_ptr<RigidBody> ground = world->create_rigid_body({.kind = BodyKind::STATIC});
    const std::unique_ptr<Collider> floor = ground->add_collider({.shape = box({.x = 20, .y = 0.5F, .z = 20}), .pose = {.position = {.y = -0.5F}}});
    std::vector<std::unique_ptr<RigidBody>> balls;
    std::vector<std::unique_ptr<Collider>> colliders;
    for (int index = 0; index < 5; ++index)
    {
        balls.push_back(world->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = static_cast<float>(3 * index), .y = 0.9F}}}));
        colliders.push_back(balls.back()->add_collider(
            {.shape = sphere(1), .filter = {.category = index == 4 ? LayerMask::LAYER_3 : LayerMask::LAYER_1, .mask = ALL_LAYERS}}));
    }

    // Nothing is published before the first step.
    EXPECT_EQ(world->ray_cast({.origin = {.y = 10}, .direction = {.y = -1}}, 100).error().kind, QueryFailureKind::NO_HIT);
    world->step(STEP);

    // The nearest hit, and every hit nearest first (with too small an output: the nearest, and CAPACITY_EXCEEDED).
    const Ray along{.origin = {.x = -5, .y = 0.9F}, .direction = {.x = 1}};
    const QueryResult<RayHit> first = world->ray_cast(along, 100);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->collider, colliders[0]->handle());
    EXPECT_NEAR(first->distance, 4.0F, 1e-4F);
    EXPECT_NEAR(first->normal.x, -1.0F, 1e-4F);
    std::array<RayHit, 8> hits{};
    const QueryResult<std::span<RayHit>> all = world->ray_cast_all(along, 100, hits);
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 5U);
    for (std::size_t index = 0; index < 5; ++index)
    {
        EXPECT_EQ((*all)[index].collider, colliders[index]->handle());
    }
    std::array<RayHit, 2> few{};
    EXPECT_EQ(world->ray_cast_all(along, 100, few).error().kind, QueryFailureKind::CAPACITY_EXCEEDED);
    EXPECT_EQ(few[0].collider, colliders[0]->handle());
    EXPECT_EQ(few[1].collider, colliders[1]->handle());

    // Filters: the query as a collider of LAYER_2 that collides with LAYER_3 only finds the last ball.
    const QueryOptions only_layer_3{.filter = {.category = LayerMask::LAYER_2, .mask = LayerMask::LAYER_3}, .engine = QueryEngine::ABSTRACTION};
    EXPECT_EQ(world->ray_cast(along, 100, only_layer_3)->collider, colliders[4]->handle());

    // A sphere cast down onto the floor between balls; overlaps by shape and by box, in handle order.
    const Shape probe{SphereShape{.radius = 0.3F}};
    const QueryResult<ShapeCastHit> landing = world->shape_cast(probe, {.position = {.x = 1.5F, .y = 5}}, {.y = -10});
    ASSERT_TRUE(landing.has_value());
    EXPECT_EQ(landing->collider, floor->handle());
    EXPECT_NEAR(landing->fraction, 0.47F, 1e-3F);
    std::array<ColliderHandle, 8> found{};
    const QueryResult<std::span<ColliderHandle>> touching = world->overlap(probe, {.position = {.x = 3, .y = 0.2F}}, found);
    ASSERT_TRUE(touching.has_value());
    EXPECT_EQ(std::vector(touching->begin(), touching->end()), (std::vector{floor->handle(), colliders[1]->handle()}));
    const QueryResult<std::span<ColliderHandle>> boxed = world->overlap(Aabb::from_center({.x = 6, .y = 1}, {.x = 3.5F, .y = 0.5F, .z = 0.5F}), found);
    ASSERT_TRUE(boxed.has_value());
    EXPECT_EQ(boxed->size(), 3U);
    std::array<ColliderHandle, 1> one{};
    EXPECT_EQ(world->overlap(Aabb::from_center({.x = 6, .y = 1}, {.x = 3.5F, .y = 0.5F, .z = 0.5F}), one).error().kind, QueryFailureKind::CAPACITY_EXCEEDED);
    const std::array<Vector3, 3> corners{Vector3{}, {.x = 1}, {.z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 1> triangle{{{0, 2, 1}}};
    const Shape mesh{TriangleMeshShape{{.vertices = corners, .triangles = triangle}}};
    EXPECT_EQ(world->shape_cast(mesh, {}, {.x = 1}).error().kind, QueryFailureKind::UNSUPPORTED) << "only convex shapes are cast";

    // The nearest collider to a point (none within too small a distance).
    const QueryResult<ClosestPointHit> nearest = world->closest_point({.x = 6.2F, .y = 4}, 10);
    ASSERT_TRUE(nearest.has_value());
    EXPECT_EQ(nearest->collider, colliders[2]->handle());
    EXPECT_NEAR(nearest->distance, length(Vector3{.x = 0.2F, .y = 3.1F}) - 1.0F, 1e-3F);
    EXPECT_EQ(world->closest_point({.x = 6, .y = 40}, 5).error().kind, QueryFailureKind::NO_HIT);

    // Contacts the backend reported: each ball touches the floor (seen from either collider's body).
    std::array<ContactInfo, 8> contacts{};
    world->step(STEP);
    const QueryResult<std::span<ContactInfo>> of_ball = world->contacts_of(balls[2]->handle(), contacts);
    ASSERT_TRUE(of_ball.has_value());
    ASSERT_EQ(of_ball->size(), 1U);
    EXPECT_EQ((*of_ball)[0].collider, colliders[2]->handle());
    EXPECT_EQ((*of_ball)[0].other_body, ground->handle());
    EXPECT_EQ(world->contacts_of(ground->handle(), contacts)->size(), 5U);
    EXPECT_EQ(world->contacts_of(RigidBodyHandle{}, contacts).error().kind, QueryFailureKind::INVALID_HANDLE);
}

// ===================================================================================================== joints, triggers, soft bodies, pipeline

TEST(SteppedWorldTest, JointsTriggersAndSoftBodiesKeepTheirDescriptionsAndStates)
{
    auto [world, backend] = make_recording_world();
    const std::unique_ptr<RigidBody> first = world->create_rigid_body({});
    const std::unique_ptr<RigidBody> second = world->create_rigid_body({.pose = {.position = {.x = 2}}});

    // A breakable hinge: its axis normalized when recorded; broken by the backend at the first step (an event, a state).
    const std::unique_ptr<Joint> hinge = world->create_joint(hinge_joint(
        {.first = first->handle(), .second = second->handle(), .anchor = {.x = 1}, .axis = {.z = 3}, .limits = JointLimits{.lower = -1, .upper = 1}, .breaking = {.force = 50}}));
    EXPECT_EQ(hinge->kind(), JointKind::HINGE);
    EXPECT_EQ(std::get<HingeJointDescription>(hinge->description()).axis, (Vector3{.z = 1}));
    world->step(STEP);
    EXPECT_TRUE(hinge->state().broken);
    ASSERT_EQ(world->events().joints.size(), 1U);
    EXPECT_EQ(world->events().joints[0].joint, hinge->handle());
    hinge->set_enabled(true);
    world->step(STEP);
    EXPECT_FALSE(world->events().joints.empty()) << "it breaks again";

    // Updates keep the kind and the bodies.
    expect_physics_error([&] { hinge->update(SliderJointDescription{.first = first->handle(), .second = second->handle()}); }, "HINGE");
    expect_physics_error([&] { hinge->update(HingeJointDescription{.first = second->handle()}); }, "other bodies");
    hinge->update(HingeJointDescription{.first = first->handle(), .second = second->handle(), .axis = {.y = 1}});
    world->step(STEP);
    EXPECT_FALSE(std::get<HingeJointDescription>(hinge->description()).limits.has_value());

    // A trigger following a body; a soft body's particles, pins and positions.
    const std::unique_ptr<Trigger> zone = world->create_trigger({.shape = sphere(2), .pose = {.position = {.y = 1}}, .body = second->handle()});
    world->step(STEP);
    EXPECT_NEAR(zone->world_bounds().center().y, second->state().pose.position.y + 1.0F, 1e-5F);
    const std::unique_ptr<SoftBody> rope = world->create_soft_body(
        {.particles = {{.position = {}}, {.position = {.x = 1}}, {.position = {.x = 2}}},
         .constraints = {{.kind = SoftBodyConstraintKind::STRETCH, .particles = {0, 1}}, {.kind = SoftBodyConstraintKind::STRETCH, .particles = {1, 2}}}});
    EXPECT_EQ(rope->particle_count(), 3U);
    rope->pin({.particle = 0, .body = first->handle()});
    rope->move_particle(2, {.x = 5});
    world->step(STEP);
    std::array<Vector3, 3> positions{};
    EXPECT_EQ(rope->positions(positions)[2], (Vector3{.x = 5}));
    EXPECT_EQ(rope->description().pins.size(), 1U);
    std::array<Vector3, 2> short_output{};
    expect_physics_error([&] { (void)rope->positions(short_output); }, "holds 2 values for 3 particles");
    expect_physics_error([&] { rope->move_particle(3, {}); }, "particle 3 of 3");
}

TEST(SteppedWorldTest, TheFrameworkStepsSoftBodiesForBackendsWithoutTheirOwn)
{
    // A backend that keeps its soft bodies has no soft-body integrator to give.
    {
        auto [world, backend] = make_recording_world();
        expect_capability_error([&] { (void)world->soft_body_integrator(); }, "SOFT_BODIES");
        expect_capability_error([&] { world->replace_soft_body_integrator(make_integrator(IntegratorKind::VERLET)); }, "SOFT_BODIES");
    }

    // One that lets the framework step them: a rope from a pin to a dynamic body holds the body up (the impulses reach
    // the backend's records), whatever integrator predicts the rope.
    auto [world, backend] = make_recording_world({}, true);
    EXPECT_EQ(world->soft_body_integrator().kind(), IntegratorKind::XPBD);
    const std::unique_ptr<RigidBody> weight = world->create_rigid_body({.pose = {.position = {.x = 1, .y = 2}}});
    const std::unique_ptr<Collider> weight_collider = weight->add_collider({.shape = sphere(0.05F), .filter = {.group = -1}});
    SoftBodyDescription description = rope_preset({.segments = 10, .length = 1.0F, .origin = {.y = 2}, .direction = {.x = 1}, .mass = 0.2F});
    description.pins = {pin_to_world(description, 0), pin_to_body(10, weight->handle(), {})};
    description.filter = {.group = -1};
    description.damping = 0.5F;
    add_long_range_attachments(description);
    const std::unique_ptr<SoftBody> rope = world->create_soft_body(description);
    float farthest = 0.0F;
    for (std::uint32_t step = 0; step < 120; ++step)
    {
        if (step == 60)
        {
            world->replace_soft_body_integrator(make_integrator(IntegratorKind::VERLET));
            EXPECT_EQ(world->soft_body_integrator().kind(), IntegratorKind::VERLET);
        }
        world->step(STEP);
        farthest = std::max(farthest, length(weight->state().pose.position - Vector3{.y = 2}));
    }
    EXPECT_LT(weight->state().pose.position.y, 1.9F) << "it swung down";
    EXPECT_LT(farthest, 1.03F) << "held by the rope";
    std::array<Vector3, 11> positions{};
    EXPECT_LT(rope->positions(positions)[5].y, 2.0F);
    EXPECT_EQ(positions[0], (Vector3{.y = 2}));
    EXPECT_EQ(world->statistics().soft_bodies, 1U);
    EXPECT_GT(world->statistics().phase_seconds[std::to_underlying(StepPhase::SOFT_BODIES)], 0.0);

    // Replacing needs an integrator; constraints naming a particle twice are refused when recorded.
    expect_physics_error([&] { world->replace_soft_body_integrator(nullptr); }, "null");
    expect_physics_error([&] { (void)world->create_soft_body({.particles = {{}, {}}, .constraints = {{.particles = {1, 1}}}}); }, "particle 1 twice");
}

/// One row that holds nothing.
class EmptyConstraint final : public UserConstraint
{
public:
    explicit EmptyConstraint(std::uint32_t rows = 1) noexcept
        : m_rows{rows}
    {
    }

    [[nodiscard]] auto row_count() const noexcept -> std::uint32_t override
    {
        return m_rows;
    }

    void rows(const ConstraintFrame& /*first*/,
              const ConstraintFrame& /*second*/,
              std::span<ConstraintRow> /*rows*/) const noexcept override
    {
    }

private:
    std::uint32_t m_rows;
};

TEST(SteppedWorldTest, UserConstraintsAreRecordedAndGoWithTheirBodies)
{
    auto [world, backend] = make_recording_world();
    std::unique_ptr<RigidBody> first = world->create_rigid_body({});
    const std::unique_ptr<RigidBody> second = world->create_rigid_body({});
    const auto rows = std::make_shared<EmptyConstraint>();

    // Recorded, applied, enabled and disabled by edits; its description as made.
    const std::unique_ptr<Constraint> constraint = world->create_constraint({.first = first->handle(), .second = second->handle(), .constraint = rows});
    EXPECT_EQ(constraint->bodies(), (std::pair{first->handle(), second->handle()}));
    constraint->set_enabled(false);
    world->step(STEP);
    EXPECT_FALSE(constraint->state().enabled);
    EXPECT_EQ(constraint->description().constraint, rows);
    const std::vector<AppliedEdit> applied = backend->applied();
    EXPECT_EQ(std::ranges::count(applied, edit_kind<edits::CreateConstraint>(), &AppliedEdit::kind), 1);
    EXPECT_EQ(std::ranges::count(applied, edit_kind<edits::SetConstraintEnabled>(), &AppliedEdit::kind), 1);

    // Destroyed with its first body: its object then says so.
    first.reset();
    world->step(STEP);
    EXPECT_EQ(std::ranges::count(backend->applied(), edit_kind<edits::DestroyConstraint>(), &AppliedEdit::kind), 1);
    expect_physics_error([&] { (void)constraint->state(); }, "was destroyed with its body");

    // Invalid constraints.
    expect_physics_error([&] { (void)world->create_constraint({.first = second->handle()}); }, "null");
    expect_physics_error([&] { (void)world->create_constraint({.first = second->handle(), .constraint = std::make_shared<EmptyConstraint>(MAXIMUM_CONSTRAINT_ROWS + 1)}); },
                         "rows");
    expect_physics_error([&] { (void)world->create_constraint({.first = second->handle(), .second = second->handle(), .constraint = rows}); }, "to itself");
}

TEST(SteppedWorldTest, ThePipelineIsTheBackendsOwn)
{
    auto [world, backend] = make_recording_world();
    EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::BACKEND);
    EXPECT_FALSE(world->broad_phase().is_external());
    EXPECT_EQ(world->solver().kind(), SolverKind::SEQUENTIAL_IMPULSE);
    EXPECT_EQ(world->integrator().kind(), IntegratorKind::SEMI_IMPLICIT_EULER);
    expect_capability_error([&] { (void)world->broad_phase().insert({}, {}, {}); }, "CUSTOM_BROAD_PHASE");
    expect_capability_error([&] { world->replace_broad_phase(std::make_unique<SpatialBroadPhaseComponent<Bvh>>()); }, "CUSTOM_BROAD_PHASE");
    expect_capability_error([&] { world->replace_solver(nullptr); }, "CUSTOM_SOLVER");
    expect_capability_error([&] { world->solver().solve({}); }, "CUSTOM_SOLVER");
    expect_capability_error([&] { world->integrator().predict({}, STEP); }, "CUSTOM_INTEGRATOR");

    // The abstraction's components work on their own.
    SpatialBroadPhaseComponent<Bvh> broad_phase;
    EXPECT_TRUE(broad_phase.is_external());
    EXPECT_EQ(broad_phase.kind(), BroadPhaseKind::BVH);
    const GjkNarrowPhase narrow_phase;
    const Shape ball{SphereShape{.radius = 1}};
    ContactCollector contacts;
    narrow_phase.collide({.shape = &ball, .pose = {}}, {.shape = &ball, .pose = {.position = {.x = 1.5F}}}, contacts, {});
    EXPECT_EQ(contacts.manifolds().size(), 1U);
}

// ===================================================================================================== checks

TEST(SteppedWorldTest, BackendsRefuseWhatTheyCannotSimulateWhenItIsRecorded)
{
    auto [world, backend] = make_recording_world();
    const std::unique_ptr<RigidBody> first = world->create_rigid_body({});
    const std::unique_ptr<RigidBody> ground = world->create_rigid_body({.kind = BodyKind::STATIC});
    expect_capability_error([&] { (void)world->create_joint(six_degrees_of_freedom_joint({.first = first->handle()})); }, "SIX_DEGREES_OF_FREEDOM_JOINTS");

    // A mesh on a static body only, and that body stays static.
    const std::array<Vector3, 3> corners{Vector3{}, {.x = 1}, {.z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 1> triangle{{{0, 2, 1}}};
    const auto mesh = make_shape(TriangleMeshShape{{.vertices = corners, .triangles = triangle}});
    expect_capability_error([&] { (void)first->add_collider({.shape = mesh}); }, "DYNAMIC_TRIANGLE_MESHES");
    const std::unique_ptr<Collider> terrain = ground->add_collider({.shape = mesh});
    expect_capability_error([&] { ground->set_kind(BodyKind::DYNAMIC); }, "DYNAMIC_TRIANGLE_MESHES");
    world->step(STEP);
    EXPECT_EQ(backend->applied().size(), 3U) << "two bodies and the terrain: refused edits are not recorded";
}

TEST(SteppedWorldTest, InvalidValuesAreRefusedWithDescriptiveErrors)
{
    auto [world, backend] = make_recording_world();
    const std::unique_ptr<RigidBody> body = world->create_rigid_body({});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    expect_physics_error([&] { (void)world->create_rigid_body({.pose = {.position = {.x = nan}}}); }, "the body's pose");
    expect_physics_error([&] { (void)world->create_rigid_body({.pose = {.orientation = {.w = 0}}}); }, "not a finite, non-zero quaternion");
    expect_physics_error([&] { (void)world->create_rigid_body({.linear_damping = -1}); }, "the linear damping");
    expect_physics_error([&] { (void)body->add_collider({}); }, "has no shape");
    expect_physics_error([&] { (void)body->add_collider({.shape = sphere(1), .density = -2}); }, "the density");
    expect_physics_error([&] { (void)body->add_collider({.shape = sphere(1), .material = {.restitution = 2}}); }, "restitution");
    expect_physics_error([&] { (void)world->create_joint(hinge_joint({.first = body->handle(), .second = body->handle()})); }, "to itself");
    expect_physics_error([&] { (void)world->create_joint(hinge_joint({.first = body->handle(), .axis = {}})); }, "the zero vector");
    expect_physics_error([&] { (void)world->create_joint(slider_joint({.first = body->handle(), .limits = JointLimits{.lower = 1, .upper = 0}})); }, "above the upper");
    expect_physics_error([&] { (void)world->create_soft_body({}); }, "no particle");
    expect_physics_error([&] { (void)world->create_soft_body({.particles = {{}}, .constraints = {{.particles = {0, 4}}}}); }, "names particle 4 of 1");
    expect_physics_error([&] { (void)world->create_trigger({}); }, "has no shape");
    expect_physics_error([&] { world->set_gravity({.y = nan}); }, "the gravity");
    expect_physics_error([&] { world->set_solver_description({.sub_steps = 0}); }, "sub-steps");
    expect_physics_error([&] { body->move_kinematic({}, STEP); }, "not KINEMATIC");
    expect_physics_error([&] { body->set_mass_properties(MassProperties{.mass = 0}); }, "the mass");
    expect_physics_error([&] { (void)SteppedWorld({}, nullptr); }, "the backend is null");

    // Errors name the backend, the operation and the objects.
    try
    {
        body->set_linear_velocity({.x = nan});
        ADD_FAILURE() << "no error";
    }
    catch (const PhysicsError& error)
    {
        const std::string_view message = error.what();
        EXPECT_NE(message.find("[gfx::physics::CUSTOM] RigidBody::set_linear_velocity"), std::string_view::npos) << message;
        EXPECT_NE(message.find(std::format("{}", body->handle())), std::string_view::npos) << message;
    }
    world->step(STEP);
    EXPECT_EQ(backend->applied().size(), 1U) << "only the valid body";
}

TEST(SteppedWorldTest, KinematicBodiesReachTheirTargets)
{
    auto [world, backend] = make_recording_world();
    const std::unique_ptr<RigidBody> platform = world->create_rigid_body({.kind = BodyKind::KINEMATIC});
    world->step(STEP);
    const Transform target{.position = {.x = 1, .y = 2, .z = -1}, .orientation = Quaternion::from_axis_angle({.y = 1}, 0.3F)};
    platform->move_kinematic(target, STEP);
    world->step(STEP);
    EXPECT_NEAR(platform->state().pose.position.x, 1.0F, 1e-5F);
    EXPECT_NEAR(platform->state().pose.position.z, -1.0F, 1e-5F);
    EXPECT_NEAR(platform->state().angular_velocity.y, 0.3F / STEP, 1e-2F);
    EXPECT_GT(dot(platform->state().pose.orientation, target.orientation), 0.999F);
}

} // namespace
