// The Box3D backend: what the factory makes (its own solver and broad phase, the broad phase's structure not shown, an
// invalid description refused). Its simulations, events, queries, threads and refusals are the conformance suite's
// (gfx_physics_conformance_tests), run on every backend.
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

[[nodiscard]] auto
box3d_world(const PhysicsWorldDescription& description = {}) -> std::unique_ptr<PhysicsWorld>
{
    return create_physics_world(description, PhysicsBackendKind::BOX3D);
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

} // namespace
