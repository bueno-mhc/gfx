// The enumerations and constants of the physics and camera libraries: described, verified at compile time, spelled and
// parsed; the layer mask is a 64-bit bitmask; the backends and subsystems name themselves in errors.
#include <gtest/gtest.h>

import std;
import gfx.core.errors;
import gfx.physics.enums;

namespace
{

using namespace gfx::physics;

/// Whether every enumerator of @p Enumeration is spelled by to_string() and parsed back by from_string().
template<class Enumeration>
auto
round_trips() -> bool
{
    return std::ranges::all_of(gfx::EnumerationTraits<Enumeration>::NAMES, [](const auto& entry)
    {
        return gfx::to_string(entry.value) == entry.name && gfx::from_string<Enumeration>(entry.name) == entry.value;
    });
}

TEST(PhysicsEnumerationsTest, EveryEnumeratorIsSpelledAndParsed)
{
    EXPECT_TRUE(round_trips<PhysicsBackendKind>());
    EXPECT_TRUE(round_trips<PhysicsSubsystem>());
    EXPECT_TRUE(round_trips<PhysicsCapability>());
    EXPECT_TRUE(round_trips<BodyKind>());
    EXPECT_TRUE(round_trips<ShapeKind>());
    EXPECT_TRUE(round_trips<JointKind>());
    EXPECT_TRUE(round_trips<DistanceMode>());
    EXPECT_TRUE(round_trips<ConstraintKind>());
    EXPECT_TRUE(round_trips<SolverKind>());
    EXPECT_TRUE(round_trips<IntegratorKind>());
    EXPECT_TRUE(round_trips<SpatialStructureKind>());
    EXPECT_TRUE(round_trips<BroadPhaseKind>());
    EXPECT_TRUE(round_trips<NarrowPhaseKind>());
    EXPECT_TRUE(round_trips<MotionQuality>());
    EXPECT_TRUE(round_trips<TriggerEventKind>());
    EXPECT_TRUE(round_trips<QueryFailureKind>());
    EXPECT_TRUE(round_trips<FrustumIntersection>());
    EXPECT_TRUE(round_trips<FrustumPlane>());
    EXPECT_TRUE(round_trips<PlaneSide>());
    EXPECT_TRUE(round_trips<DepthRange>());
    EXPECT_TRUE(round_trips<AxisPlane>());
    EXPECT_TRUE(round_trips<LayerMask>());
    EXPECT_TRUE(round_trips<gfx::camera::CameraBackendKind>());
    EXPECT_TRUE(round_trips<gfx::camera::CameraProjectionKind>());
    EXPECT_TRUE(round_trips<gfx::camera::CameraControllerKind>());
    EXPECT_TRUE(round_trips<gfx::camera::NormalizedDeviceYAxis>());
    EXPECT_TRUE(round_trips<gfx::camera::ReferenceFrame>());
    EXPECT_EQ(std::format("{}", PhysicsCapability::SOFT_BODIES), "SOFT_BODIES");
    EXPECT_FALSE(gfx::from_string<BodyKind>("FLOATING").has_value());
}

TEST(PhysicsEnumerationsTest, LayersAreSixtyFourBits)
{
    static_assert(gfx::BitmaskEnumeration<LayerMask> && std::same_as<std::underlying_type_t<LayerMask>, std::uint64_t>);
    static_assert(gfx::EnumerationTraits<LayerMask>::NAMES.size() == 64, "one enumerator per layer (NONE is no bit)");
    static_assert(std::to_underlying(LayerMask::LAYER_63) == 1ULL << 63U);
    static_assert(has_all(ALL_LAYERS, LayerMask::LAYER_0 | LayerMask::LAYER_63));
    constexpr LayerMask players = LayerMask::LAYER_1 | LayerMask::LAYER_40;
    static_assert(has_any(players, LayerMask::LAYER_40) && !has_any(players, LayerMask::LAYER_2));
    static_assert(is_empty(players & ~players));
    EXPECT_EQ(to_string(players), "LAYER_1 | LAYER_40");
    EXPECT_EQ(gfx::from_string<LayerMask>("LAYER_1 | LAYER_40"), players);
}

TEST(PhysicsEnumerationsTest, BackendsAndSubsystemsNameTheirLibrary)
{
    // Errors print "[gfx::physics::<NAME>]": the library comes from the traits.
    static_assert(gfx::BackendEnumeration<PhysicsBackendKind> && gfx::BackendEnumeration<PhysicsSubsystem>);
    const gfx::BackendIdentity backend = PhysicsBackendKind::BOX3D;
    const gfx::BackendIdentity subsystem = PhysicsSubsystem::NARROW_PHASE;
    EXPECT_EQ(std::format("{}", backend), "gfx::physics::BOX3D");
    EXPECT_EQ(std::format("{}", subsystem), "gfx::physics::NARROW_PHASE");
    EXPECT_TRUE(backend == PhysicsBackendKind::BOX3D);
    EXPECT_FALSE(backend == PhysicsBackendKind::REFERENCE);

    // The camera's enumerations share the file, in their own namespace and library.
    static_assert(gfx::BackendEnumeration<gfx::camera::CameraBackendKind>);
    EXPECT_EQ(std::format("{}", gfx::BackendIdentity{gfx::camera::CameraBackendKind::STANDARD}), "gfx::camera::STANDARD");
    static_assert(gfx::camera::INFINITE_FAR_PLANE == std::numeric_limits<float>::infinity());
}

TEST(PhysicsEnumerationsTest, ConstantsHoldTheirDocumentedValues)
{
    static_assert(MAXIMUM_MANIFOLD_POINTS == 4);
    static_assert(DEFAULT_RESTITUTION_THRESHOLD == 1.0F);
    static_assert(GEOMETRIC_TOLERANCE > 0 && GEOMETRIC_TOLERANCE < 1e-3F);

    // The orders other code relies on: Frustum::planes, the batch kernels' outputs.
    static_assert(std::to_underlying(FrustumPlane::LEFT) == 0 && std::to_underlying(FrustumPlane::FAR_PLANE) == 5);
    static_assert(std::to_underlying(FrustumIntersection::OUTSIDE) == 0 && std::to_underlying(FrustumIntersection::INSIDE) == 2);
    SUCCEED();
}

} // namespace
