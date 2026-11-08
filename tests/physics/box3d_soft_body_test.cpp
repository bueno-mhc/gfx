// The abstraction's XPBD soft bodies in Box3D worlds: a light rope holds a Box3D body twenty times its mass up (two-way
// through a pin) and lets it fall when unpinned. The other soft-body scenarios (a cloth resting on a table, a lighter body
// held, momentum exchanged through contacts, the same bits on any threads) are the conformance suite's, on every backend.
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

} // namespace
