// The conformance suite, soft bodies: on every backend (the GPU simulates its own, the others the abstraction's XPBD solver
// coupled with their bodies), a cloth falls onto a table and rests on it, never through it; a rope holds a body at its end
// and lets it fall when unpinned; a soft cube thrown at a floating body pushes it and keeps the momentum; a cloth and a
// rope holding a body give the same bits twice, and on four threads where the backend uses them.
#include "conformance_test.hpp"

namespace gfx::tests::conformance
{
namespace
{

using SoftBodyConformanceTest = ConformanceTest;

/// @brief The largest speed of @p soft_body's particles.
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

/// @brief A floor (top at y = 0) and a static box of half extents 0.3 on it, colliders of one static body.
struct Stage
{
    std::unique_ptr<RigidBody> ground;
    std::unique_ptr<Collider> floor;
    std::unique_ptr<Collider> table;
};

[[nodiscard]] auto
make_stage(PhysicsWorld& world) -> Stage
{
    Stage stage{.ground = world.create_rigid_body({.kind = BodyKind::STATIC}), .floor = nullptr, .table = nullptr};
    stage.floor = stage.ground->add_collider({.shape = box({.x = 20, .y = 0.5F, .z = 20}), .pose = {.position = {.y = -0.5F}}});
    stage.table = stage.ground->add_collider({.shape = box({.x = 0.3F, .y = 0.3F, .z = 0.3F}), .pose = {.position = {.y = 0.3F}}});
    return stage;
}

TEST_P(SoftBodyConformanceTest, AClothFallsOntoATableAndRests)
{
    // A 24 × 24 cloth dropped over the table: over it, on it (in folds a few centimeters high at most), never through the
    // table or the floor, at rest after five seconds.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Stage stage = make_stage(*made);
    SoftBodyDescription description = cloth_preset({.columns = 24, .rows = 24, .spacing = 0.04F, .origin = {.x = -0.46F, .y = 1.0F, .z = -0.46F}, .row_direction = {.z = 1}});
    description.damping = 0.5F;
    const std::unique_ptr<SoftBody> cloth = made->create_soft_body(description);
    advance(*made, 300);
    std::vector<Vector3> positions(cloth->particle_count());
    (void)cloth->positions(positions);
    const float radius = description.particle_radius;
    for (const Vector3 position : positions)
    {
        EXPECT_GE(position.y, radius - 2.0e-3F) << "not through the floor";
        if (std::abs(position.x) < 0.28F && std::abs(position.z) < 0.28F)
        {
            EXPECT_GE(position.y, 0.6F + radius - 2.0e-3F) << "not through the table";
            EXPECT_LE(position.y, 0.6F + radius + 0.04F) << "on the table";
        }
    }
    EXPECT_LT(fastest(*cloth), 0.05F) << "at rest";
    EXPECT_GT(made->statistics().phase_seconds[std::to_underlying(StepPhase::SOFT_BODIES)], 0.0);
}

TEST_P(SoftBodyConformanceTest, ARopeHoldsABodyUpAndLetsItFall)
{
    // A 1 kg box hanging from a 1 m rope of 0.2 kg pinned to the world: it stays at the rope's end, at rest; unpinned from
    // the rope, it falls.
    const std::unique_ptr<PhysicsWorld> made = world();
    const Body weight = make_body(*made, {.pose = {.position = {.y = 2}}}, {.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 125.0F, .filter = {.group = -1}});
    SoftBodyDescription description = rope_preset({.segments = 10, .length = 1.0F, .origin = {.y = 3}, .mass = 0.2F});
    description.pins = {pin_to_world(description, 0), pin_to_body(10, weight.body->handle(), {})};
    description.filter = {.group = -1};
    description.damping = 0.5F;
    add_long_range_attachments(description);
    const std::unique_ptr<SoftBody> rope = made->create_soft_body(description);
    advance(*made, 300);
    EXPECT_NEAR(weight.body->mass_properties().mass, 1.0F, 1.0e-3F);
    EXPECT_NEAR(weight.body->state().pose.position.y, 2.0F, 0.03F) << "held at the rope's end";
    EXPECT_LT(length(weight.body->state().linear_velocity), 0.05F);
    rope->unpin(10);
    advance(*made, 30);
    EXPECT_LT(weight.body->state().pose.position.y, 1.0F) << "fallen once unpinned";
}

TEST_P(SoftBodyConformanceTest, ASoftCubePushesABodyWithTheMomentumItLoses)
{
    // No gravity: a soft cube thrown at 2 m/s at a 2 kg box floating at rest pushes it, and the momentum of the two is the
    // cube's at the start (within 2 %).
    const std::unique_ptr<PhysicsWorld> made = world({.gravity = {}});
    const Body target = make_body(*made, {.pose = {.position = {.x = 0.8F}}, .allow_sleep = false},
                                  {.shape = box({.x = 0.25F, .y = 0.25F, .z = 0.25F}), .material = {.friction = {.static_coefficient = 0, .dynamic_coefficient = 0}}, .density = 16.0F});
    SoftBodyDescription description = volume_preset(tetrahedral_box({.half_extents = {.x = 0.15F, .y = 0.15F, .z = 0.15F}, .cells = {2, 2, 2}}), {.density = 200.0F});
    description.damping = 0.0F;
    description.friction = 0.0F;
    float cube_mass = 0.0F;
    for (SoftBodyParticle& particle : description.particles)
    {
        particle.velocity = {.x = 2};
        cube_mass += 1.0F / particle.inverse_mass;
    }
    const std::unique_ptr<SoftBody> cube = made->create_soft_body(description);
    advance(*made, 60);
    std::vector<Vector3> velocities(cube->particle_count());
    Vector3 momentum = target.body->state().linear_velocity * target.body->mass_properties().mass;
    const std::span<Vector3> particle_velocities = cube->velocities(velocities);
    for (std::size_t index = 0; index < particle_velocities.size(); ++index)
    {
        momentum += particle_velocities[index] / description.particles[index].inverse_mass;
    }
    EXPECT_NEAR(target.body->mass_properties().mass, 2.0F, 1.0e-3F);
    EXPECT_GT(target.body->state().linear_velocity.x, 0.5F) << "pushed";
    EXPECT_NEAR(momentum.x, cube_mass * 2.0F, 0.02F * cube_mass * 2.0F) << "momentum kept";
}

TEST_P(SoftBodyConformanceTest, SoftBodiesGiveTheSameBitsWhateverTheThreads)
{
    // A 48 × 48 cloth onto the table and a rope holding a box, 60 steps: twice, and with a scheduler of four threads, the
    // same particles and the same body.
    const auto run_scene = [this](physics::TaskScheduler* scheduler) {
        const std::unique_ptr<PhysicsWorld> made = world({.task_scheduler = scheduler});
        const Stage stage = make_stage(*made);
        const Body weight = make_body(*made, {.pose = {.position = {.x = 1, .y = 2}}}, {.shape = box({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 500.0F});
        SoftBodyDescription rope = rope_preset({.segments = 8, .length = 1.0F, .origin = {.x = 1, .y = 3}});
        rope.pins = {pin_to_world(rope, 0), pin_to_body(8, weight.body->handle(), {})};
        const std::unique_ptr<SoftBody> rope_body = made->create_soft_body(rope);
        SoftBodyDescription cloth = cloth_preset({.columns = 48, .rows = 48, .spacing = 0.02F, .origin = {.x = -0.47F, .y = 1, .z = -0.47F}, .row_direction = {.z = 1}});
        const std::unique_ptr<SoftBody> cloth_body = made->create_soft_body(cloth);
        advance(*made, 60);
        std::vector<Vector3> positions(cloth_body->particle_count() + rope_body->particle_count());
        (void)cloth_body->positions(std::span{positions}.first(cloth_body->particle_count()));
        (void)rope_body->positions(std::span{positions}.subspan(cloth_body->particle_count()));
        positions.push_back(weight.body->state().pose.position);
        return positions;
    };
    const std::vector<Vector3> alone = run_scene(nullptr);
    EXPECT_TRUE(alone == run_scene(nullptr)) << "twice";
    ThreadTaskScheduler threads{4};
    EXPECT_TRUE(alone == run_scene(&threads)) << "on four threads";
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         SoftBodyConformanceTest,
                         testing::ValuesIn(conformance_backends()),
                         conformance_test_name);

} // namespace
} // namespace gfx::tests::conformance
