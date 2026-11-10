// The GPU backend's events, contacts, triggers and queries (phase G8), on each GPU of the machine with the Vulkan
// validation layers, in both solver modes: contact BEGIN, HIT and END as the reference backend reports them (the same
// scene on both backends begins the same pairs within a step or two), contacts_of() from the body's side for awake and
// sleeping bodies, a destroyed collider's touches ended, hits bouncing off within a step reported, collider flags turning
// events off; trigger ENTER, STAY and EXIT as the reference backend reports them; the step's phases timed on the GPU; every
// BACKEND query against the abstraction's answer on a scene of every shape kind, and the queries' filters.
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

/// A world with its bodies and colliders (the colliders destroyed first, then the bodies, then the world).
struct Scene
{
    std::unique_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<RigidBody>> bodies{};
    std::vector<std::unique_ptr<Collider>> colliders{};
    std::vector<ContactEvent> contact_events{};

    auto add(const RigidBodyDescription& body,
             const ColliderDescription& collider) -> RigidBody&
    {
        bodies.push_back(world->create_rigid_body(body));
        colliders.push_back(bodies.back()->add_collider(collider));
        return *bodies.back();
    }

    /// A static floor, its top at y = 0.
    auto floor() -> RigidBody&
    {
        return add({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, {.shape = box_shape({.x = 30, .y = 0.5F, .z = 30})});
    }

    /// Steps, keeping the contact events.
    void run(int steps)
    {
        for (int step = 0; step < steps; ++step)
        {
            world->step(STEP);
            contact_events.insert(contact_events.end(), world->events().contacts.begin(), world->events().contacts.end());
        }
    }

    /// Events of @p kind kept between @p first and @p second (either order).
    [[nodiscard]] auto count(ContactEventKind kind,
                             ColliderHandle first,
                             ColliderHandle second) const -> std::ptrdiff_t
    {
        return std::ranges::count_if(contact_events, [&](const ContactEvent& event) {
            return event.kind == kind && std::minmax(event.first, event.second) == std::minmax(first, second);
        });
    }
};

/// The contacts of @p body (at most 16).
auto
contacts_of(const PhysicsWorld& world,
            const RigidBody& body) -> std::vector<ContactInfo>
{
    std::array<ContactInfo, 16> storage{};
    const QueryResult<std::span<ContactInfo>> found = world.contacts_of(body.handle(), storage);
    return found ? std::vector<ContactInfo>(found->begin(), found->end()) : std::vector<ContactInfo>{};
}

// ===================================================================================================== fixture

/// A world on each GPU in each solver mode, with the validation layers.
class GpuXpbdEventTest : public gfx::tests::VulkanDeviceTest,
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

    [[nodiscard]] auto scene(const PhysicsWorldDescription& description = {}) -> Scene
    {
        return Scene{.world = gpu::create_physics_world(description, *m_device, {.solver_mode = mode()})};
    }
};

// ===================================================================================================== contacts

TEST_P(GpuXpbdEventTest, ABallLandingBeginsHitsAndEndsWhenItsColliderGoes)
{
    // Dropped from 3 m: one BEGIN with the floor (the pair in either order), a HIT (landing at 7 m/s), the contact from
    // the ball's side pointing down; its collider destroyed, the touch ends at the next step.
    Scene world = scene({.hit_event_speed = 1.0F});
    const RigidBody& floor = world.floor();
    RigidBody& ball = world.add({.pose = {.position = {.y = 3.0F}}}, {.shape = sphere_shape(0.25F)});
    const ColliderHandle floor_collider = world.colliders[0]->handle();
    const ColliderHandle ball_collider = world.colliders[1]->handle();
    world.run(90);
    EXPECT_EQ(world.count(ContactEventKind::BEGIN, floor_collider, ball_collider), 1);
    EXPECT_GE(world.count(ContactEventKind::HIT, floor_collider, ball_collider), 1);
    EXPECT_EQ(world.count(ContactEventKind::END, floor_collider, ball_collider), 0);
    const auto begin = std::ranges::find(world.contact_events, ContactEventKind::BEGIN, &ContactEvent::kind);
    ASSERT_NE(begin, world.contact_events.end());
    EXPECT_NEAR(begin->point.y, 0.0F, 0.05F) << "on the floor";
    EXPECT_NEAR(std::abs(begin->normal.y), 1.0F, 1.0e-3F);
    const auto hit = std::ranges::find(world.contact_events, ContactEventKind::HIT, &ContactEvent::kind);
    ASSERT_NE(hit, world.contact_events.end());
    EXPECT_GT(hit->approach_speed, 5.0F) << "landing from 3 m";

    const std::vector<ContactInfo> touching = contacts_of(*world.world, ball);
    ASSERT_EQ(touching.size(), 1U);
    EXPECT_EQ(touching[0].collider, ball_collider);
    EXPECT_EQ(touching[0].other, floor_collider);
    EXPECT_EQ(touching[0].other_body, floor.handle());
    EXPECT_NEAR(touching[0].manifold.normal.y, -1.0F, 1.0e-3F) << "from the ball to the floor";
    EXPECT_EQ(contacts_of(*world.world, floor).size(), 1U);

    world.contact_events.clear();
    world.colliders[1].reset();
    world.run(1);
    EXPECT_EQ(world.count(ContactEventKind::END, floor_collider, ball_collider), 1);
    EXPECT_TRUE(contacts_of(*world.world, ball).empty());
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, SleepingStacksKeepTheirContacts)
{
    // Five boxes asleep on the floor: the bottom one touches the floor and the box above (4 points each), though the GPU no
    // longer looks at pairs of resting bodies; no END while they sleep; a push wakes them, and they still touch.
    Scene world = scene();
    world.floor();
    std::vector<RigidBody*> boxes;
    for (int level = 0; level < 5; ++level)
    {
        boxes.push_back(&world.add({.pose = {.position = {.y = 0.5F + static_cast<float>(level) * 1.001F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
    }
    world.run(300);
    ASSERT_FALSE(boxes.front()->state().awake) << "the stack sleeps";
    const auto ends = std::ranges::count(world.contact_events, ContactEventKind::END, &ContactEvent::kind);
    EXPECT_EQ(ends, 0);
    EXPECT_EQ(std::ranges::count(world.contact_events, ContactEventKind::BEGIN, &ContactEvent::kind), 5) << "each box on what is under it";
    const std::vector<ContactInfo> touching = contacts_of(*world.world, *boxes.front());
    ASSERT_EQ(touching.size(), 2U);
    for (const ContactInfo& contact : touching)
    {
        EXPECT_EQ(contact.collider, world.colliders[1]->handle());
        EXPECT_GE(contact.manifold.point_count, 3U);
    }
    EXPECT_GE(world.world->statistics().contacts, 16U) << "the sleeping stack's points";

    // (A sleeping island wakes one contact layer per step: G5 as built, deviation 4.)
    boxes.front()->apply_linear_impulse({.x = 0.5F});
    world.run(5);
    EXPECT_TRUE(boxes.back()->state().awake);
    EXPECT_EQ(contacts_of(*world.world, *boxes.front()).size(), 2U);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, HitsThatBounceOffWithinAStepAreReported)
{
    // A bouncy ball at 60 m/s toward a wall, without gravity: its speculative contact stops and returns it within one
    // step, so its points never end a step within the slop; the solver pushed the pair, so BEGIN, HIT and END come.
    Scene world = scene({.gravity = {}});
    const RigidBody& wall = world.add({.kind = BodyKind::STATIC}, {.shape = box_shape({.x = 2.0F, .y = 2.0F, .z = 0.1F})});
    RigidBody& ball = world.add({.pose = {.position = {.z = 2.7F}}, .linear_velocity = {.z = -60.0F}, .motion_quality = MotionQuality::CONTINUOUS},
                                {.shape = sphere_shape(0.25F), .material = {.restitution = 1.0F}});
    (void)wall;
    world.run(10);
    const ColliderHandle wall_collider = world.colliders[0]->handle();
    const ColliderHandle ball_collider = world.colliders[1]->handle();
    EXPECT_GT(ball.state().linear_velocity.z, 50.0F) << "bounced back";
    EXPECT_EQ(world.count(ContactEventKind::BEGIN, wall_collider, ball_collider), 1);
    EXPECT_EQ(world.count(ContactEventKind::HIT, wall_collider, ball_collider), 1);
    EXPECT_EQ(world.count(ContactEventKind::END, wall_collider, ball_collider), 1);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, CollidersWithoutContactEventsStillTouch)
{
    // A box whose collider makes no events lands: no event, yet its contact is there.
    Scene world = scene();
    world.floor();
    RigidBody& crate = world.add({.pose = {.position = {.y = 1.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .contact_events = false});
    world.run(60);
    EXPECT_TRUE(world.contact_events.empty());
    EXPECT_EQ(contacts_of(*world.world, crate).size(), 1U);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, ContactEventsMatchTheReferenceBackends)
{
    // The same scene on the reference backend and on the GPU: balls and boxes dropped on the floor, a ball on a box, two
    // boxes sliding into each other. The same pairs begin, each within two steps of the reference's step, none ends; the
    // same pairs touch at the end.
    const auto build = [](Scene& target) {
        target.floor();
        for (int index = 0; index < 4; ++index)
        {
            const float x = -6.0F + 3.0F * static_cast<float>(index);
            (void)target.add({.pose = {.position = {.x = x, .y = 1.0F + 0.5F * static_cast<float>(index)}}}, {.shape = sphere_shape(0.3F)});
            (void)target.add({.pose = {.position = {.x = x, .y = 0.6F, .z = 3.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
        }
        (void)target.add({.pose = {.position = {.x = 6.0F, .y = 0.5F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
        (void)target.add({.pose = {.position = {.x = 6.0F, .y = 2.5F}}}, {.shape = sphere_shape(0.3F)});
        (void)target.add({.pose = {.position = {.x = -3.0F, .y = 0.5F, .z = -3.0F}}, .linear_velocity = {.x = 3.0F}},
                         {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
        (void)target.add({.pose = {.position = {.x = 0.0F, .y = 0.5F, .z = -3.0F}}, .linear_velocity = {.x = -3.0F}},
                         {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
    };
    Scene reference{.world = create_physics_world({}, PhysicsBackendKind::REFERENCE)};
    Scene gpu_world = scene();
    build(reference);
    build(gpu_world);

    // The events by step, the colliders named by their index (the worlds' handles may differ).
    using Begun = std::map<std::pair<std::size_t, std::size_t>, int>;
    const auto index_of = [](const Scene& target, ColliderHandle collider) {
        return static_cast<std::size_t>(std::ranges::find(target.colliders, collider, [](const std::unique_ptr<Collider>& item) { return item->handle(); }) -
                                        target.colliders.begin());
    };
    const auto run = [&](Scene& target, Begun& begun, int& ends) {
        for (int step = 0; step < 120; ++step)
        {
            target.world->step(STEP);
            for (const ContactEvent& event : target.world->events().contacts)
            {
                const std::size_t first = index_of(target, event.first);
                const std::size_t second = index_of(target, event.second);
                const std::pair pair{std::min(first, second), std::max(first, second)};
                if (event.kind == ContactEventKind::BEGIN)
                {
                    begun.try_emplace(pair, step);
                }
                ends += event.kind == ContactEventKind::END ? 1 : 0;
            }
        }
    };
    Begun reference_begun;
    Begun gpu_begun;
    int reference_ends = 0;
    int gpu_ends = 0;
    run(reference, reference_begun, reference_ends);
    run(gpu_world, gpu_begun, gpu_ends);
    EXPECT_EQ(reference_ends, 0);
    EXPECT_EQ(gpu_ends, 0);
    ASSERT_EQ(gpu_begun.size(), reference_begun.size());
    for (const auto& [pair, step] : reference_begun)
    {
        const auto found = gpu_begun.find(pair);
        ASSERT_NE(found, gpu_begun.end()) << "colliders " << pair.first << " and " << pair.second;
        EXPECT_LE(std::abs(found->second - step), 2) << "colliders " << pair.first << " and " << pair.second;
    }
    for (std::size_t body = 1; body < gpu_world.bodies.size(); ++body)
    {
        EXPECT_EQ(contacts_of(*gpu_world.world, *gpu_world.bodies[body]).size(), contacts_of(*reference.world, *reference.bodies[body]).size()) << "body " << body;
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

// ===================================================================================================== triggers

TEST_P(GpuXpbdEventTest, ABallFallingThroughATriggerEntersStaysAndExits)
{
    // A ball falls through a box-shaped zone: ENTER once (the zone and the ball), STAY while inside, EXIT once; resting in a
    // second zone on the floor, its collider destroyed: EXIT at once.
    Scene world = scene();
    world.floor();
    (void)world.add({.pose = {.position = {.y = 3.0F}}}, {.shape = sphere_shape(0.25F)});
    const ColliderHandle ball = world.colliders[1]->handle();
    const std::unique_ptr<Trigger> zone = world.world->create_trigger({.shape = box_shape({.x = 1.0F, .y = 0.5F, .z = 1.0F}), .pose = {.position = {.y = 1.5F}}});
    const std::unique_ptr<Trigger> ground = world.world->create_trigger({.shape = box_shape({.x = 1.0F, .y = 0.2F, .z = 1.0F}), .pose = {.position = {.y = 0.25F}}});
    std::vector<TriggerEvent> events;
    for (int step = 0; step < 90; ++step)
    {
        world.world->step(STEP);
        events.insert(events.end(), world.world->events().triggers.begin(), world.world->events().triggers.end());
    }
    const auto count = [&events](TriggerHandle trigger, TriggerEventKind kind) {
        return std::ranges::count_if(events, [&](const TriggerEvent& event) { return event.trigger == trigger && event.kind == kind; });
    };
    EXPECT_EQ(count(zone->handle(), TriggerEventKind::ENTER), 1);
    EXPECT_GE(count(zone->handle(), TriggerEventKind::STAY), 5);
    EXPECT_EQ(count(zone->handle(), TriggerEventKind::EXIT), 1);
    EXPECT_EQ(count(ground->handle(), TriggerEventKind::ENTER), 1);
    EXPECT_EQ(count(ground->handle(), TriggerEventKind::EXIT), 0);
    for (const TriggerEvent& event : events)
    {
        EXPECT_EQ(event.collider, ball) << "the floor is not in the zones (5 cm under the lower one: touching would count)";
    }

    world.colliders[1].reset();
    world.world->step(STEP);
    const std::span<const TriggerEvent> last = world.world->events().triggers;
    ASSERT_EQ(last.size(), 1U);
    EXPECT_EQ(last[0].kind, TriggerEventKind::EXIT);
    EXPECT_EQ(last[0].trigger, ground->handle());
    EXPECT_EQ(last[0].collider, ball);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, TriggersFollowTheirBodiesAndFilters)
{
    // A trigger on a kinematic body sweeps along a row of static boxes: it detects them one after the other (not its own
    // body's collider), except the box whose category its filter leaves out; a zone overlapping the floor stays in it.
    Scene world = scene({.gravity = {}});
    world.floor();
    for (int index = 0; index < 4; ++index)
    {
        const CollisionFilter filter{.category = index == 2 ? LayerMask::LAYER_2 : LayerMask::LAYER_1, .mask = ALL_LAYERS};
        (void)world.add({.kind = BodyKind::STATIC, .pose = {.position = {.x = 2.0F * static_cast<float>(index), .y = 1.0F}}},
                        {.shape = box_shape({.x = 0.25F, .y = 0.25F, .z = 0.25F}), .filter = filter});
    }
    RigidBody& carrier = world.add({.kind = BodyKind::KINEMATIC, .pose = {.position = {.x = -2.0F, .y = 1.0F}}, .linear_velocity = {.x = 2.0F}},
                                   {.shape = sphere_shape(0.1F)});
    const std::unique_ptr<Trigger> sweep = world.world->create_trigger(
        {.shape = sphere_shape(0.5F), .pose = {.position = {.y = 0.0F}}, .body = carrier.handle(), .filter = {.category = ALL_LAYERS, .mask = ~LayerMask::LAYER_2}});
    const std::unique_ptr<Trigger> ground = world.world->create_trigger({.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .pose = {.position = {.x = 10.0F}}});
    std::vector<ColliderHandle> entered;
    int ground_stays = 0;
    for (int step = 0; step < 300; ++step)
    {
        world.world->step(STEP);
        for (const TriggerEvent& event : world.world->events().triggers)
        {
            if (event.trigger == sweep->handle() && event.kind == TriggerEventKind::ENTER)
            {
                entered.push_back(event.collider);
            }
            ground_stays += event.trigger == ground->handle() && event.kind == TriggerEventKind::STAY ? 1 : 0;
        }
    }
    EXPECT_GT(carrier.state().pose.position.x, 7.0F) << "the carrier moved along the row";
    const std::vector<ColliderHandle> expected{world.colliders[1]->handle(), world.colliders[2]->handle(), world.colliders[4]->handle()};
    EXPECT_EQ(entered, expected) << "the boxes in turn, not the third (LAYER_2), never the carrier's own sphere";
    EXPECT_EQ(ground_stays, 299) << "the floor stays in the zone every step after the first";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, TriggerEventsMatchTheReferenceBackends)
{
    // Kinematic bodies of several shapes cross zones of several shapes (one on a moving body), on the reference backend
    // and on the GPU: the same events at the same steps, in the same order.
    const auto build = [](Scene& target, std::vector<std::unique_ptr<Trigger>>& triggers) {
        target.floor();
        const std::array<std::shared_ptr<const Shape>, 3> shapes{sphere_shape(0.3F), box_shape({.x = 0.3F, .y = 0.2F, .z = 0.4F}),
                                                                  make_shape(CapsuleShape{.half_height = 0.3F, .radius = 0.2F})};
        for (int index = 0; index < 3; ++index)
        {
            const float z = 2.0F * static_cast<float>(index);
            (void)target.add({.kind = BodyKind::KINEMATIC, .pose = {.position = {.x = -3.0F, .y = 1.0F, .z = z}}, .linear_velocity = {.x = 1.5F + 0.5F * static_cast<float>(index)}},
                             {.shape = shapes[static_cast<std::size_t>(index)]});
            triggers.push_back(target.world->create_trigger({.shape = shapes[static_cast<std::size_t>(2 - index)], .pose = {.position = {.x = 0.5F * static_cast<float>(index), .y = 1.0F, .z = z}}}));
        }
        RigidBody& mover = target.add({.kind = BodyKind::KINEMATIC, .pose = {.position = {.x = 3.0F, .y = 1.0F, .z = 2.0F}}, .linear_velocity = {.x = -2.0F}},
                                      {.shape = sphere_shape(0.1F)});
        triggers.push_back(target.world->create_trigger({.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 2.5F}), .body = mover.handle()}));
    };
    Scene reference{.world = create_physics_world({.gravity = {}}, PhysicsBackendKind::REFERENCE)};
    Scene gpu_world = scene({.gravity = {}});
    std::vector<std::unique_ptr<Trigger>> reference_triggers;
    std::vector<std::unique_ptr<Trigger>> gpu_triggers;
    build(reference, reference_triggers);
    build(gpu_world, gpu_triggers);

    // The events as (step, kind, trigger index, collider index).
    using Event = std::tuple<int, TriggerEventKind, std::size_t, std::size_t>;
    const auto run = [](Scene& target, const std::vector<std::unique_ptr<Trigger>>& triggers) {
        std::vector<Event> events;
        for (int step = 0; step < 240; ++step)
        {
            target.world->step(STEP);
            for (const TriggerEvent& event : target.world->events().triggers)
            {
                const auto trigger = static_cast<std::size_t>(std::ranges::find(triggers, event.trigger, [](const std::unique_ptr<Trigger>& item) { return item->handle(); }) -
                                                              triggers.begin());
                const auto collider = static_cast<std::size_t>(std::ranges::find(target.colliders, event.collider, [](const std::unique_ptr<Collider>& item) {
                                                                   return item->handle();
                                                               }) - target.colliders.begin());
                events.emplace_back(step, event.kind, trigger, collider);
            }
        }
        return events;
    };
    const std::vector<Event> expected = run(reference, reference_triggers);
    const std::vector<Event> found = run(gpu_world, gpu_triggers);
    EXPECT_GT(std::ranges::count(expected, TriggerEventKind::ENTER, [](const Event& event) { return std::get<1>(event); }), 4);
    EXPECT_EQ(found, expected);
    EXPECT_TRUE(take_validation_messages().empty());
}

// ===================================================================================================== statistics

TEST_P(GpuXpbdEventTest, PhasesAreTimedOnTheGpu)
{
    // Boxes falling on a floor beside a hanging cloth: with timestamps, each phase the GPU runs takes some time, together
    // less than the step's wall time; without, the solver's phase is the step's wall time.
    Scene world = scene();
    world.floor();
    for (int index = 0; index < 8; ++index)
    {
        (void)world.add({.pose = {.position = {.x = static_cast<float>(index) * 1.5F, .y = 2.0F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
    }
    SoftBodyDescription cloth = cloth_preset({.columns = 8, .rows = 8, .spacing = 0.1F, .origin = {.x = -3.0F, .y = 2.0F}});
    cloth.pins = {pin_to_world(cloth, cloth_particle(8, 0, 0)), pin_to_world(cloth, cloth_particle(8, 7, 0))};
    const std::unique_ptr<SoftBody> soft = world.world->create_soft_body(cloth);
    world.run(10);
    const auto start = std::chrono::steady_clock::now();
    world.world->step(STEP);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const StepStatistics statistics = world.world->statistics();
    const auto seconds = [&statistics](StepPhase phase) { return statistics.phase_seconds[static_cast<std::size_t>(std::to_underlying(phase))]; };
    if (gpu::capabilities(*m_device).timestamps)
    {
        double sum = 0.0;
        for (const StepPhase phase : {StepPhase::BROAD_PHASE, StepPhase::NARROW_PHASE, StepPhase::SOLVER, StepPhase::SOFT_BODIES, StepPhase::EVENTS})
        {
            EXPECT_GT(seconds(phase), 0.0) << gfx::to_string(phase);
            sum += seconds(phase);
        }
        EXPECT_LT(sum, wall);
        std::cout << std::format("{} | {} | GPU phases (ms): broad {:.3f}, narrow {:.3f}, solver {:.3f}, soft bodies {:.3f}, events {:.3f}; step {:.3f}\n",
                                 m_physical_device->properties().name, mode(), 1000.0 * seconds(StepPhase::BROAD_PHASE), 1000.0 * seconds(StepPhase::NARROW_PHASE),
                                 1000.0 * seconds(StepPhase::SOLVER), 1000.0 * seconds(StepPhase::SOFT_BODIES), 1000.0 * seconds(StepPhase::EVENTS), 1000.0 * wall);
    }
    else
    {
        EXPECT_GT(seconds(StepPhase::SOLVER), 0.0);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

// ===================================================================================================== queries

/// A world of every shape kind for the queries: a floor; a row of static shapes (sphere, capsule, cylinder, cone, box,
/// rounded box, hull, compound of a nested compound) turned about two axes; a two-sided mesh ramp, a one-sided mesh roof,
/// a bumpy height field; boxes and balls resting on the floor; a box only LAYER_3 queries see.
struct QueryScene
{
    Scene world;
    std::shared_ptr<const Shape> hull{};
};

auto
query_scene(Scene world) -> QueryScene
{
    world.floor();
    const std::shared_ptr<const Shape> ball = sphere_shape(0.2F);
    const std::shared_ptr<const Shape> inner = make_shape(CompoundShape{{CompoundChild{.shape = ball, .pose = {.position = {.y = 0.3F}}},
                                                                         CompoundChild{.shape = box_shape({.x = 0.1F, .y = 0.3F, .z = 0.1F})}}});
    const std::vector<Vector3> corners{{.x = -0.4F, .y = -0.3F, .z = -0.4F}, {.x = 0.4F, .y = -0.3F, .z = -0.4F}, {.x = 0.0F, .y = -0.3F, .z = 0.5F},
                                       {.x = 0.0F, .y = 0.4F, .z = 0.0F}, {.x = 0.2F, .y = 0.1F, .z = 0.3F}};
    const std::shared_ptr<const Shape> hull = make_shape(HullShape::from_points(corners));
    const std::vector<std::shared_ptr<const Shape>> shapes{
        sphere_shape(0.4F),
        make_shape(CapsuleShape{.half_height = 0.4F, .radius = 0.25F}),
        make_shape(CylinderShape{.half_height = 0.35F, .radius = 0.35F}),
        make_shape(ConeShape{.half_height = 0.4F, .radius = 0.4F}),
        box_shape({.x = 0.4F, .y = 0.3F, .z = 0.5F}),
        make_shape(BoxShape{.half_extents = {.x = 0.35F, .y = 0.25F, .z = 0.45F}, .radius = 0.05F}),
        hull,
        make_shape(CompoundShape{{CompoundChild{.shape = inner, .pose = {.position = {.x = -0.4F}}}, CompoundChild{.shape = ball, .pose = {.position = {.x = 0.4F}}}}}),
    };
    for (std::size_t index = 0; index < shapes.size(); ++index)
    {
        const Quaternion turn = gfx::normalized(Quaternion::from_axis_angle(gfx::normalized(Vector3{.x = 1, .y = 0.5F, .z = 0.2F}), 0.4F * static_cast<float>(index)));
        (void)world.add({.kind = BodyKind::STATIC, .pose = {.position = {.x = -7.0F + 2.0F * static_cast<float>(index), .y = 1.5F}, .orientation = turn}},
                        {.shape = shapes[index]});
    }

    // Meshes: a two-sided ramp, a one-sided roof (front up); a bumpy height field.
    const std::vector<Vector3> quad{{.x = -1.5F, .z = -1.5F}, {.x = 1.5F, .z = -1.5F}, {.x = 1.5F, .z = 1.5F}, {.x = -1.5F, .z = 1.5F}};
    const std::vector<std::array<std::uint32_t, 3>> triangles{{0, 2, 1}, {0, 3, 2}};
    (void)world.add({.kind = BodyKind::STATIC, .pose = {.position = {.x = -4.0F, .y = 1.0F, .z = 4.0F}, .orientation = Quaternion::from_axis_angle({.x = 1}, 0.3F)}},
                    {.shape = make_shape(TriangleMeshShape{{.vertices = quad, .triangles = triangles, .two_sided = true}})});
    (void)world.add({.kind = BodyKind::STATIC, .pose = {.position = {.x = 0.0F, .y = 2.5F, .z = 4.0F}}},
                    {.shape = make_shape(TriangleMeshShape{{.vertices = quad, .triangles = triangles}})});
    static std::vector<float> samples = [] {
        std::vector<float> heights(11 * 11);
        for (std::size_t row = 0; row < 11; ++row)
        {
            for (std::size_t column = 0; column < 11; ++column)
            {
                heights[row * 11 + column] = 0.3F * std::sin(0.9F * static_cast<float>(row)) * std::cos(0.7F * static_cast<float>(column));
            }
        }
        return heights;
    }();
    (void)world.add({.kind = BodyKind::STATIC, .pose = {.position = {.x = 3.0F, .y = 0.4F, .z = 3.0F}}},
                    {.shape = make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{samples.data(), 11, 11},
                                                           .scale = {.x = 0.4F, .y = 1.0F, .z = 0.4F}}})});

    // Resting bodies (the moving tree); a box only LAYER_3 queries see.
    for (int index = 0; index < 4; ++index)
    {
        (void)world.add({.pose = {.position = {.x = -6.0F + 3.0F * static_cast<float>(index), .y = 0.3F, .z = -3.0F}}},
                        {.shape = index % 2 == 0 ? box_shape({.x = 0.3F, .y = 0.3F, .z = 0.3F}) : sphere_shape(0.3F)});
    }
    (void)world.add({.kind = BodyKind::STATIC, .pose = {.position = {.x = 6.0F, .y = 0.5F, .z = -3.0F}}},
                    {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F}), .filter = {.category = LayerMask::LAYER_3, .mask = ALL_LAYERS}});
    world.run(20);
    return {.world = std::move(world), .hull = hull};
}

/// Whether two values agree within @p tolerance.
auto
close(float first,
      float second,
      float tolerance) -> bool
{
    return std::abs(first - second) <= tolerance || (std::isinf(first) && std::isinf(second));
}

TEST_P(GpuXpbdEventTest, RayCastsMatchTheAbstractions)
{
    // 400 rays from random points toward random targets (half with an unbounded maximum): the GPU's nearest hit is the
    // abstraction's (the same collider, distance within 2 mm, the same sub-shape), and every hit of a ray_cast_all alike;
    // some differ in their last bits (the GPU answered: the backend did not hand the queries back to the abstraction).
    QueryScene scene = query_scene(this->scene());
    const PhysicsWorld& world = *scene.world.world;
    std::mt19937 generator{7};
    std::uniform_real_distribution<float> coordinate{-8.0F, 8.0F};
    std::uniform_real_distribution<float> height{-0.5F, 4.0F};
    int hits = 0;
    int rounded_otherwise = 0;
    for (int index = 0; index < 400; ++index)
    {
        const Vector3 start{.x = coordinate(generator), .y = height(generator) + 2.0F, .z = coordinate(generator) * 0.6F};
        const Vector3 target{.x = coordinate(generator), .y = height(generator), .z = coordinate(generator) * 0.6F};
        const Ray ray = Ray::from_points(start, target);
        const float maximum = index % 2 == 0 ? 25.0F : std::numeric_limits<float>::infinity();
        const QueryResult<RayHit> gpu_hit = world.ray_cast(ray, maximum, {.engine = QueryEngine::BACKEND});
        const QueryResult<RayHit> cpu_hit = world.ray_cast(ray, maximum, {.engine = QueryEngine::ABSTRACTION});
        ASSERT_EQ(gpu_hit.has_value(), cpu_hit.has_value()) << "ray " << index;
        if (!cpu_hit)
        {
            continue;
        }
        ++hits;
        EXPECT_EQ(gpu_hit->collider, cpu_hit->collider) << "ray " << index;
        EXPECT_TRUE(close(gpu_hit->distance, cpu_hit->distance, 2.0e-3F)) << "ray " << index << ": " << gpu_hit->distance << " vs " << cpu_hit->distance;
        EXPECT_GT(gfx::dot(gpu_hit->normal, cpu_hit->normal), 0.99F) << "ray " << index;
        EXPECT_EQ(gpu_hit->sub_shape, cpu_hit->sub_shape) << "ray " << index;
        rounded_otherwise += gpu_hit->distance != cpu_hit->distance || gpu_hit->normal != cpu_hit->normal ? 1 : 0;

        std::array<RayHit, 32> gpu_all{};
        std::array<RayHit, 32> cpu_all{};
        const QueryResult<std::span<RayHit>> gpu_list = world.ray_cast_all(ray, maximum, gpu_all, {.engine = QueryEngine::BACKEND});
        const QueryResult<std::span<RayHit>> cpu_list = world.ray_cast_all(ray, maximum, cpu_all, {.engine = QueryEngine::ABSTRACTION});
        ASSERT_TRUE(gpu_list.has_value() && cpu_list.has_value());
        EXPECT_EQ(gpu_list->size(), cpu_list->size()) << "ray " << index;
        if (gpu_list->size() != cpu_list->size())
        {
            continue;
        }
        for (std::size_t hit = 0; hit < cpu_list->size(); ++hit)
        {
            EXPECT_EQ((*gpu_list)[hit].collider, (*cpu_list)[hit].collider) << "ray " << index << ", hit " << hit;
            EXPECT_TRUE(close((*gpu_list)[hit].distance, (*cpu_list)[hit].distance, 2.0e-3F)) << "ray " << index << ", hit " << hit;
        }
    }
    EXPECT_GT(hits, 150);
    EXPECT_GT(rounded_otherwise, 0) << "every answer bit for bit the abstraction's";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, ShapeQueriesMatchTheAbstractions)
{
    // From random poses: casts of a sphere, a box, a capsule and the scene's hull (a shape of the table) along random
    // translations, overlaps of the same shapes and of boxes, closest points within 5 m and unbounded: the GPU's answers
    // are the abstraction's (casts' fractions within 1e-3, closest distances within 2 mm, the same colliders); some differ
    // in their last bits (the GPU answered).
    QueryScene scene = query_scene(this->scene());
    const PhysicsWorld& world = *scene.world.world;
    const std::array<std::shared_ptr<const Shape>, 4> shapes{sphere_shape(0.3F), box_shape({.x = 0.3F, .y = 0.2F, .z = 0.4F}),
                                                             make_shape(CapsuleShape{.half_height = 0.3F, .radius = 0.2F}), scene.hull};
    std::mt19937 generator{11};
    std::uniform_real_distribution<float> coordinate{-8.0F, 8.0F};
    std::uniform_real_distribution<float> height{0.0F, 3.0F};
    std::uniform_real_distribution<float> angle{-3.0F, 3.0F};
    int casts = 0;
    int overlaps = 0;
    int casts_rounded_otherwise = 0;
    int closest_rounded_otherwise = 0;
    for (int index = 0; index < 200; ++index)
    {
        const Shape& shape = *shapes[static_cast<std::size_t>(index) % shapes.size()];
        const Transform pose{.position = {.x = coordinate(generator), .y = height(generator), .z = coordinate(generator) * 0.6F},
                             .orientation = gfx::normalized(Quaternion::from_axis_angle(gfx::normalized(Vector3{.x = 0.3F, .y = 1.0F, .z = -0.2F}), angle(generator)))};
        const Vector3 translation{.x = coordinate(generator) * 0.5F, .y = -height(generator), .z = coordinate(generator) * 0.3F};
        const QueryResult<ShapeCastHit> gpu_cast = world.shape_cast(shape, pose, translation, {.engine = QueryEngine::BACKEND});
        const QueryResult<ShapeCastHit> cpu_cast = world.shape_cast(shape, pose, translation, {.engine = QueryEngine::ABSTRACTION});
        ASSERT_EQ(gpu_cast.has_value(), cpu_cast.has_value()) << "cast " << index;
        if (cpu_cast)
        {
            ++casts;
            EXPECT_EQ(gpu_cast->collider, cpu_cast->collider) << "cast " << index;
            EXPECT_TRUE(close(gpu_cast->fraction, cpu_cast->fraction, 1.0e-3F)) << "cast " << index << ": " << gpu_cast->fraction << " vs " << cpu_cast->fraction;
            casts_rounded_otherwise += gpu_cast->fraction != cpu_cast->fraction ? 1 : 0;
        }

        std::array<ColliderHandle, 16> gpu_found{};
        std::array<ColliderHandle, 16> cpu_found{};
        const auto gpu_overlap = world.overlap(shape, pose, gpu_found, {.engine = QueryEngine::BACKEND});
        const auto cpu_overlap = world.overlap(shape, pose, cpu_found, {.engine = QueryEngine::ABSTRACTION});
        ASSERT_TRUE(gpu_overlap.has_value() && cpu_overlap.has_value());
        EXPECT_TRUE(std::ranges::equal(*gpu_overlap, *cpu_overlap)) << "overlap " << index;
        overlaps += cpu_overlap->empty() ? 0 : 1;

        const gfx::physics::Aabb box{.minimum = pose.position - Vector3{.x = 1, .y = 1, .z = 1}, .maximum = pose.position + Vector3{.x = 1, .y = 0.5F, .z = 1}};
        const auto gpu_boxes = world.overlap(box, gpu_found, {.engine = QueryEngine::BACKEND});
        const auto cpu_boxes = world.overlap(box, cpu_found, {.engine = QueryEngine::ABSTRACTION});
        ASSERT_TRUE(gpu_boxes.has_value() && cpu_boxes.has_value());
        EXPECT_TRUE(std::ranges::equal(*gpu_boxes, *cpu_boxes)) << "box " << index;

        for (const float maximum : {5.0F, std::numeric_limits<float>::infinity()})
        {
            const QueryResult<ClosestPointHit> gpu_closest = world.closest_point(pose.position, maximum, {.engine = QueryEngine::BACKEND});
            const QueryResult<ClosestPointHit> cpu_closest = world.closest_point(pose.position, maximum, {.engine = QueryEngine::ABSTRACTION});
            ASSERT_EQ(gpu_closest.has_value(), cpu_closest.has_value()) << "closest " << index;
            if (cpu_closest)
            {
                EXPECT_EQ(gpu_closest->collider, cpu_closest->collider) << "closest " << index;
                EXPECT_TRUE(close(gpu_closest->distance, cpu_closest->distance, 2.0e-3F)) << "closest " << index;
                EXPECT_LT(gfx::length(gpu_closest->point - cpu_closest->point), 5.0e-3F) << "closest " << index;
                closest_rounded_otherwise += gpu_closest->distance != cpu_closest->distance ? 1 : 0;
            }
        }
    }
    EXPECT_GT(casts, 50);
    EXPECT_GT(overlaps, 10);
    EXPECT_GT(casts_rounded_otherwise, 0) << "every cast bit for bit the abstraction's";
    EXPECT_GT(closest_rounded_otherwise, 0) << "every closest point bit for bit the abstraction's";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdEventTest, QueriesFollowTheirFilters)
{
    // The box only LAYER_3 sees: a ray straight at it hits the floor behind it unless the query's mask holds LAYER_3.
    QueryScene scene = query_scene(this->scene());
    const PhysicsWorld& world = *scene.world.world;
    const Ray ray{.origin = {.x = 6.0F, .y = 3.0F, .z = -3.0F}, .direction = {.y = -1.0F}};
    const QueryResult<RayHit> hidden = world.ray_cast(ray, 10.0F, {.filter = {.category = ALL_LAYERS, .mask = LayerMask::LAYER_0}});
    ASSERT_TRUE(hidden.has_value());
    EXPECT_NEAR(hidden->distance, 3.0F, 1.0e-3F) << "the floor";
    const QueryResult<RayHit> seen = world.ray_cast(ray, 10.0F, {.filter = {.category = ALL_LAYERS, .mask = LayerMask::LAYER_3}});
    ASSERT_TRUE(seen.has_value());
    EXPECT_NEAR(seen->distance, 2.0F, 1.0e-3F) << "the box";
    EXPECT_TRUE(take_validation_messages().empty());
}

auto
event_test_name(const testing::TestParamInfo<std::tuple<std::size_t, GpuSolverMode>>& parameter) -> std::string
{
    return std::format("Device{}_{}", std::get<0>(parameter.param), std::get<1>(parameter.param));
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdEventTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         event_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdEventTest);

// ===================================================================================================== measurements

class GpuXpbdEventMeasurementTest : public GpuXpbdEventTest
{
};

TEST_P(GpuXpbdEventMeasurementTest, EventsAndQueriesAreMeasured)
{
    // 400 balls and boxes dropped in a 20 × 20 grid through 100 zones onto the floor: the mean step time over 60 steps
    // after 30 and its events phase (contacts reported, events, triggers); then one query's round trip on the GPU and on
    // the CPU (the abstraction), a ray straight down onto the pile and a closest point beside it, 100 of each.
    Scene world = scene();
    world.floor();
    for (int row = 0; row < 20; ++row)
    {
        for (int column = 0; column < 20; ++column)
        {
            const Vector3 position{.x = -4.75F + 0.5F * static_cast<float>(column), .y = 1.0F + 0.1F * static_cast<float>((row + column) % 7), .z = -4.75F + 0.5F * static_cast<float>(row)};
            (void)world.add({.pose = {.position = position}}, {.shape = (row + column) % 2 == 0 ? sphere_shape(0.2F) : box_shape({.x = 0.18F, .y = 0.18F, .z = 0.18F})});
        }
    }
    std::vector<std::unique_ptr<Trigger>> zones;
    for (int row = 0; row < 10; ++row)
    {
        for (int column = 0; column < 10; ++column)
        {
            zones.push_back(world.world->create_trigger(
                {.shape = sphere_shape(0.4F), .pose = {.position = {.x = -4.5F + 1.0F * static_cast<float>(column), .y = 0.6F, .z = -4.5F + 1.0F * static_cast<float>(row)}}}));
        }
    }
    world.run(30);
    const auto start = std::chrono::steady_clock::now();
    double events_seconds = 0.0;
    std::size_t trigger_events = 0;
    for (int step = 0; step < 60; ++step)
    {
        world.world->step(STEP);
        events_seconds += world.world->statistics().phase_seconds[static_cast<std::size_t>(std::to_underlying(StepPhase::EVENTS))];
        trigger_events += world.world->events().triggers.size();
    }
    const double step_milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60.0;

    const PhysicsWorld& physics = *world.world;
    const auto query_milliseconds = [&physics](QueryEngine engine) {
        const auto begin = std::chrono::steady_clock::now();
        std::size_t answers = 0;
        for (int index = 0; index < 100; ++index)
        {
            const float x = -4.5F + 0.09F * static_cast<float>(index);
            answers += physics.ray_cast(Ray{.origin = {.x = x, .y = 5.0F, .z = 0.3F}, .direction = {.y = -1.0F}}, 10.0F, {.engine = engine}).has_value() ? 1U : 0U;
            answers += physics.closest_point({.x = 6.0F, .y = 0.5F, .z = x}, 3.0F, {.engine = engine}).has_value() ? 1U : 0U;
        }
        return std::pair{std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / 200.0, answers};
    };
    const auto [gpu_query, gpu_answers] = query_milliseconds(QueryEngine::BACKEND);
    const auto [cpu_query, cpu_answers] = query_milliseconds(QueryEngine::ABSTRACTION);
    EXPECT_EQ(gpu_answers, cpu_answers);
    EXPECT_GT(trigger_events, 0U);
    std::cout << std::format("{} | {} | 400 bodies, 100 zones: {:.2f} ms per step (events phase {:.3f} ms, {} contact points); a query {:.3f} ms on the GPU, "
                             "{:.3f} ms on the CPU\n",
                             m_physical_device->properties().name, mode(), step_milliseconds, 1000.0 * events_seconds / 60.0, physics.statistics().contacts, gpu_query,
                             cpu_query);
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdEventMeasurementTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         event_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdEventMeasurementTest);

} // namespace
