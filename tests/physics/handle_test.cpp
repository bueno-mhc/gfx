// The handles of the physics library: one kind per tag (kinds do not convert), grouped by the concepts the interfaces
// take, trivially copyable, ordered, hashed and formatted with the kind's name.
#include <gtest/gtest.h>

import std;
import gfx.physics.handles;
import gfx.tests.physics_support;

namespace
{

using namespace gfx::physics;
using gfx::tests::PhysicsHandles;

TEST(PhysicsHandlesTest, KindsAreDistinctAndGrouped)
{
    // Every kind is a physics handle except the camera's.
    static_assert(PhysicsHandle<WorldHandle> && PhysicsHandle<RigidBodyHandle> && PhysicsHandle<ContactManifoldHandle> && PhysicsHandle<IntegratorHandle>);
    static_assert(!PhysicsHandle<CameraHandle> && !PhysicsHandle<CameraControllerHandle> && !PhysicsHandle<std::uint32_t>);

    // The groups the interfaces take.
    static_assert(BodyHandle<RigidBodyHandle> && BodyHandle<SoftBodyHandle> && !BodyHandle<ColliderHandle>);
    static_assert(CollisionObjectHandle<ColliderHandle> && CollisionObjectHandle<TriggerHandle> && !CollisionObjectHandle<ShapeHandle>);
    static_assert(ConstraintObjectHandle<ConstraintHandle> && ConstraintObjectHandle<JointHandle> && !ConstraintObjectHandle<RigidBodyHandle>);
    static_assert(SpatialHandle<SpatialStructureHandle> && SpatialHandle<SpatialProxyHandle> && !SpatialHandle<BroadPhaseHandle>);
    static_assert(CameraObjectHandle<CameraHandle> && CameraObjectHandle<CameraControllerHandle> && !CameraObjectHandle<WorldHandle>);

    // No conversion between kinds, nor from or to integers.
    static_assert(!std::is_convertible_v<RigidBodyHandle, SoftBodyHandle> && !std::is_constructible_v<ColliderHandle, TriggerHandle>);
    static_assert(!std::is_constructible_v<WorldHandle, std::uint32_t> && !std::is_convertible_v<WorldHandle, std::uint32_t>);

    // 32 bits, copied as such.
    static_assert(sizeof(RigidBodyHandle) == 4 && std::is_trivially_copyable_v<RigidBodyHandle>);
    SUCCEED();
}

TEST(PhysicsHandlesTest, HandlesCompareHashAndFormat)
{
    const RigidBodyHandle first = PhysicsHandles::make<RigidBodyTag>(12, 3);
    const RigidBodyHandle second = PhysicsHandles::make<RigidBodyTag>(12, 4);
    EXPECT_TRUE(first.is_valid());
    EXPECT_FALSE(RigidBodyHandle{}.is_valid());
    EXPECT_NE(first, second);
    EXPECT_LT(RigidBodyHandle{}, first);
    EXPECT_EQ(first, PhysicsHandles::make<RigidBodyTag>(12, 3));
    EXPECT_EQ(std::format("{}", first), "RigidBody(index 12, generation 3)");
    EXPECT_EQ(std::format("{}", JointHandle{}), "Joint(null)");
    EXPECT_EQ(std::format("{}", PhysicsHandles::make<SpatialProxyTag>(0)), "SpatialProxy(index 0, generation 1)");

    // Hashed for unordered containers.
    std::unordered_set<RigidBodyHandle> bodies{first, second, first};
    EXPECT_EQ(bodies.size(), 2U);
}

} // namespace
