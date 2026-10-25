// The abstraction's XPBD soft bodies in Box3D worlds: a cloth falls onto Box3D colliders and rests, a rope holds a Box3D
// body up (two-way through a pin), a soft cube pushes a Box3D body with the momentum it loses (two-way through
// contacts), the same bits on one and four threads.
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

constexpr float STEP = 1.0F / 60.0F;

[[nodiscard]] auto
box(Vector3 half_extents) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half_extents});
}

/// The largest speed of @p soft_body's particles.
[[nodiscard]] auto
fastest(const SoftBody& soft_body) -> float
{
    std::vector<Vector3> velocities(soft_body.particle_count());
    float largest = 0.0F;
    for (const Vector3 velocity : soft_body.velocities(velocities))
    {
        largest = std::max(largest, length(velocity));
    }
    return largest;
}

/// A Box3D world with a static floor (its top at y = 0) and a static box of half extents 0.3 on it.
struct Stage
{
    std::unique_ptr<PhysicsWorld> world;
    std::unique_ptr<RigidBody> ground;
    std::unique_ptr<Collider> floor;
    std::unique_ptr<Collider> table;
};

[[nodiscard]] auto
make_stage(TaskScheduler* scheduler = nullptr) -> Stage
{
    Stage stage{.world = create_physics_world({.task_scheduler = scheduler}, PhysicsBackendKind::BOX3D), .ground = nullptr, .floor = nullptr, .table = nullptr};
    stage.ground = stage.world->create_rigid_body({.kind = BodyKind::STATIC});
    stage.floor = stage.ground->add_collider({.shape = box({.x = 20, .y = 0.5F, .z = 20}), .pose = {.position = {.y = -0.5F}}});
    stage.table = stage.ground->add_collider({.shape = box({.x = 0.3F, .y = 0.3F, .z = 0.3F}), .pose = {.position = {.y = 0.3F}}});
    return stage;
}

TEST(Box3dSoftBodyTest, AClothFallsOntoBox3dCollidersAndRests)
{
    Stage stage = make_stage();
    EXPECT_EQ(stage.world->soft_body_integrator().kind(), IntegratorKind::XPBD);
    SoftBodyDescription description = cloth_preset({.columns = 24, .rows = 24, .spacing = 0.04F, .origin = {.x = -0.46F, .y = 1.0F, .z = -0.46F}, .row_direction = {.z = 1}});
    description.damping = 0.5F;
    const std::unique_ptr<SoftBody> cloth = stage.world->create_soft_body(description);
    for (std::uint32_t step = 0; step < 300; ++step)
    {
        stage.world->step(STEP);
    }

    // Over the table, on it (in folds a few centimeters high at most); never through the table or the floor.
    std::vector<Vector3> positions(cloth->particle_count());
    (void)cloth->positions(positions);
    const float radius = description.particle_radius;
    for (const Vector3 position : positions)
    {
        EXPECT_GE(position.y, radius - 2.0e-3F);
        if (std::abs(position.x) < 0.28F && std::abs(position.z) < 0.28F)
        {
            EXPECT_GE(position.y, 0.6F + radius - 2.0e-3F) << "not through the table";
            EXPECT_LE(position.y, 0.6F + radius + 0.04F) << "on the table";
        }
    }
    EXPECT_LT(fastest(*cloth), 0.05F) << "at rest";
    EXPECT_GT(stage.world->statistics().phase_seconds[std::to_underlying(StepPhase::SOFT_BODIES)], 0.0);
}

TEST(Box3dSoftBodyTest, ARopeHoldsABox3dBodyUp)
{
    // A 4 kg box hanging from a 1 m rope pinned to the world: the rope pulls the box (a pin to a dynamic body takes part
    // of each correction), Box3D moves it; it stays at the rope's end, at rest.
    Stage stage = make_stage();
    const std::unique_ptr<RigidBody> weight = stage.world->create_rigid_body({.pose = {.position = {.y = 2}}});
    const std::unique_ptr<Collider> weight_collider =
        weight->add_collider({.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 500.0F, .filter = {.group = -1}});
    SoftBodyDescription description = rope_preset({.segments = 10, .length = 1.0F, .origin = {.y = 3}, .mass = 0.2F});
    description.pins = {pin_to_world(description, 0), pin_to_body(10, weight->handle(), {})};
    description.filter = {.group = -1};
    description.damping = 0.5F;
    add_long_range_attachments(description);
    const std::unique_ptr<SoftBody> rope = stage.world->create_soft_body(description);
    for (std::uint32_t step = 0; step < 180; ++step)
    {
        stage.world->step(STEP);
    }
    EXPECT_NEAR(weight->mass_properties().mass, 4.0F, 1.0e-3F);
    EXPECT_NEAR(weight->state().pose.position.y, 2.0F, 0.03F) << "held at the rope's end";
    EXPECT_LT(length(weight->state().linear_velocity), 0.05F);

    // Unpinned, it falls.
    rope->unpin(10);
    for (std::uint32_t step = 0; step < 30; ++step)
    {
        stage.world->step(STEP);
    }
    EXPECT_LT(weight->state().pose.position.y, 1.0F);
}

TEST(Box3dSoftBodyTest, ASoftCubePushesABox3dBodyWithTheMomentumItLoses)
{
    // No gravity: a soft cube thrown at a 2 kg box floating at rest.
    const std::unique_ptr<PhysicsWorld> world = create_physics_world({.gravity = {}}, PhysicsBackendKind::BOX3D);
    const std::unique_ptr<RigidBody> target = world->create_rigid_body({.pose = {.position = {.x = 0.8F}}, .allow_sleep = false});
    const std::unique_ptr<Collider> target_collider = target->add_collider({.shape = box({.x = 0.25F, .y = 0.25F, .z = 0.25F}), .material = {.friction = {.static_coefficient = 0, .dynamic_coefficient = 0}}, .density = 16.0F});
    SoftBodyDescription description = volume_preset(tetrahedral_box({.half_extents = {.x = 0.15F, .y = 0.15F, .z = 0.15F}, .cells = {2, 2, 2}}), {.density = 200.0F});
    description.damping = 0.0F;
    description.friction = 0.0F;
    float cube_mass = 0.0F;
    for (SoftBodyParticle& particle : description.particles)
    {
        particle.velocity = {.x = 2};
        cube_mass += 1.0F / particle.inverse_mass;
    }
    const std::unique_ptr<SoftBody> cube = world->create_soft_body(description);
    for (std::uint32_t step = 0; step < 60; ++step)
    {
        world->step(STEP);
    }

    std::vector<Vector3> velocities(cube->particle_count());
    Vector3 momentum = target->state().linear_velocity * target->mass_properties().mass;
    const std::span<Vector3> particle_velocities = cube->velocities(velocities);
    for (std::size_t index = 0; index < particle_velocities.size(); ++index)
    {
        momentum += particle_velocities[index] / description.particles[index].inverse_mass;
    }
    EXPECT_NEAR(target->mass_properties().mass, 2.0F, 1.0e-3F);
    EXPECT_GT(target->state().linear_velocity.x, 0.5F) << "pushed";
    EXPECT_NEAR(momentum.x, cube_mass * 2.0F, 0.02F * cube_mass * 2.0F) << "momentum kept";
}

TEST(Box3dSoftBodyTest, SoftBodiesGiveTheSameBitsOnOneAndFourThreads)
{
    // A cloth and a rope holding a box, stepped with no scheduler and with four threads (Box3D's tasks and the cloth's
    // colors): the same particles, the same body.
    const auto run = [](TaskScheduler* scheduler) {
        Stage stage = make_stage(scheduler);
        const std::unique_ptr<RigidBody> weight = stage.world->create_rigid_body({.pose = {.position = {.x = 1, .y = 2}}});
        const std::unique_ptr<Collider> weight_collider = weight->add_collider({.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 500.0F});
        SoftBodyDescription rope = rope_preset({.segments = 8, .length = 1.0F, .origin = {.x = 1, .y = 3}});
        rope.pins = {pin_to_world(rope, 0), pin_to_body(8, weight->handle(), {})};
        const std::unique_ptr<SoftBody> rope_body = stage.world->create_soft_body(rope);
        SoftBodyDescription cloth = cloth_preset({.columns = 48, .rows = 48, .spacing = 0.02F, .origin = {.x = -0.47F, .y = 1, .z = -0.47F}, .row_direction = {.z = 1}});
        const std::unique_ptr<SoftBody> cloth_body = stage.world->create_soft_body(cloth);
        for (std::uint32_t step = 0; step < 60; ++step)
        {
            stage.world->step(STEP);
        }
        std::vector<Vector3> positions(cloth_body->particle_count() + rope_body->particle_count());
        (void)cloth_body->positions(std::span{positions}.first(cloth_body->particle_count()));
        (void)rope_body->positions(std::span{positions}.subspan(cloth_body->particle_count()));
        positions.push_back(weight->state().pose.position);
        return positions;
    };
    const std::vector<Vector3> alone = run(nullptr);
    ThreadTaskScheduler threads{4};
    EXPECT_TRUE(alone == run(&threads));
}

} // namespace
