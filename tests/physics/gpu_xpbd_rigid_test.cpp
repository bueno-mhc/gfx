// The GPU backend's rigid bodies in contact (phase G5), on each GPU of the machine with the Vulkan validation layers, in
// both solver modes (graph coloring, Jacobi): a box coming to rest on a floor, stacks and pyramids standing still, static
// friction holding a box on a slope and dynamic friction letting one slide as Coulomb's law says, restitution bouncing a
// ball to the height e² predicts, islands falling asleep together and waking together, every shape kind coming to rest on
// a triangle mesh and a height field, the same steps giving the same bits, and a pile large enough to overflow the first
// step's buffers (replayed).
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
constexpr float PI = std::numbers::pi_v<float>;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<gfx::Instance> instance =
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx GPU physics tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

auto
box_shape(Vector3 half,
          float radius = 0.0F) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half, .radius = radius});
}

auto
material(float friction,
         float restitution = 0.0F) -> SurfaceMaterial
{
    return {.friction = {.static_coefficient = friction, .dynamic_coefficient = friction}, .restitution = restitution};
}

/// A world with its bodies and colliders (the colliders destroyed first, then the bodies, then the world).
struct Scene
{
    std::unique_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<RigidBody>> bodies{};
    std::vector<std::unique_ptr<Collider>> colliders{};

    auto add(const RigidBodyDescription& body,
             const ColliderDescription& collider) -> RigidBody&
    {
        bodies.push_back(world->create_rigid_body(body));
        colliders.push_back(bodies.back()->add_collider(collider));
        return *bodies.back();
    }

    /// A static floor, its top at y = 0.
    auto floor(SurfaceMaterial surface = {}) -> RigidBody&
    {
        return add({.kind = BodyKind::STATIC, .pose = {.position = {.y = -0.5F}}}, {.shape = box_shape({.x = 30, .y = 0.5F, .z = 30}), .material = surface});
    }

    void run(float seconds)
    {
        const auto steps = static_cast<int>(std::lround(seconds / STEP));
        for (int step = 0; step < steps; ++step)
        {
            world->step(STEP);
        }
    }
};

auto
finite(const RigidBodyState& state) -> bool
{
    const auto vector_finite = [](Vector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
    return vector_finite(state.pose.position) && vector_finite(state.linear_velocity) && vector_finite(state.angular_velocity);
}

/// Whether one of the axes of @p orientation points up (a box resting on a face).
auto
rests_on_a_face(const Quaternion& orientation) -> bool
{
    float best = 0.0F;
    for (const Vector3 axis : {Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}})
    {
        best = std::max(best, std::abs(gfx::rotate(orientation, axis).y));
    }
    return best > 0.999F;
}

// ===================================================================================================== fixture

/// A world on each GPU in each solver mode, with the validation layers.
class GpuXpbdRigidTest : public gfx::tests::VulkanDeviceTest,
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

    /// A stack of @p count boxes of half extent @p half on the floor of @p target (1 mm apart), from the bottom.
    static auto stack(Scene& target,
                      std::uint32_t count,
                      float half,
                      Vector3 base = {}) -> std::vector<RigidBody*>
    {
        std::vector<RigidBody*> boxes;
        for (std::uint32_t level = 0; level < count; ++level)
        {
            const float y = half + static_cast<float>(level) * (2.0F * half + 0.001F);
            boxes.push_back(&target.add({.pose = {.position = base + Vector3{.y = y}}}, {.shape = box_shape({.x = half, .y = half, .z = half})}));
        }
        return boxes;
    }
};

TEST_P(GpuXpbdRigidTest, ABoxComesToRestOnTheFloor)
{
    // Dropped from 2 m turned about two axes: it lands, settles on a face, and stops 0.5 m up.
    Scene world = scene();
    world.floor();
    RigidBody& crate = world.add({.pose = {.position = {.y = 2}, .orientation = gfx::normalized(Quaternion::from_axis_angle(gfx::normalized(Vector3{.x = 1, .z = 0.5F}), 0.4F))}},
                                 {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
    world.run(1.0F);
    EXPECT_GT(world.world->statistics().contacts, 0U) << "touching the floor (before it falls asleep)";
    world.run(2.0F);
    const RigidBodyState state = crate.state();
    ASSERT_TRUE(finite(state));
    EXPECT_NEAR(state.pose.position.y, 0.5F, 0.01F);
    EXPECT_LT(gfx::length(state.linear_velocity), 0.05F);
    EXPECT_TRUE(rests_on_a_face(state.pose.orientation));
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, StacksStandStillAndFallAsleep)
{
    // Six boxes: still after 4 s (the top one leaning less than 1 cm, lower by less than 3 cm: the soft contacts'
    // compression under the stack), all asleep.
    Scene world = scene();
    world.floor();
    const std::vector<RigidBody*> boxes = stack(world, 6, 0.5F);
    const Vector3 top = boxes.back()->state().pose.position;
    std::uint32_t asleep = 0;
    for (int step = 0; step < 240; ++step)
    {
        world.world->step(STEP);
        for (const SleepEvent& event : world.world->events().sleeps)
        {
            asleep += event.kind == SleepEventKind::FELL_ASLEEP ? 1U : 0U;
        }
    }
    const RigidBodyState state = boxes.back()->state();
    ASSERT_TRUE(finite(state));
    const Vector3 moved = state.pose.position - top;
    EXPECT_LT(std::hypot(moved.x, moved.z), 0.01F) << "the stack leaned";
    EXPECT_LT(std::abs(moved.y), 0.03F) << "the stack sank";
    for (const RigidBody* box : boxes)
    {
        EXPECT_FALSE(box->state().awake) << "every box of the stack sleeps";
        EXPECT_TRUE(rests_on_a_face(box->state().pose.orientation));
    }
    EXPECT_EQ(asleep, 6U);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, PyramidsStandStill)
{
    // A pyramid of 21 boxes (6 at its base): none moves more than 2 cm in 4 s.
    Scene world = scene();
    world.floor();
    std::vector<RigidBody*> boxes;
    std::vector<Vector3> starts;
    constexpr std::uint32_t BASE = 6;
    for (std::uint32_t row = 0; row < BASE; ++row)
    {
        for (std::uint32_t column = 0; column < BASE - row; ++column)
        {
            const Vector3 position{.x = (static_cast<float>(column) - static_cast<float>(BASE - row - 1) * 0.5F) * 1.05F,
                                   .y = 0.5F + static_cast<float>(row) * 1.001F};
            boxes.push_back(&world.add({.pose = {.position = position}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
            starts.push_back(position);
        }
    }
    world.run(4.0F);
    float farthest = 0.0F;
    for (std::size_t index = 0; index < boxes.size(); ++index)
    {
        const RigidBodyState state = boxes[index]->state();
        ASSERT_TRUE(finite(state));
        farthest = std::max(farthest, gfx::length(state.pose.position - starts[index]));
    }
    EXPECT_LT(farthest, 0.02F) << "a box of the pyramid moved";
    EXPECT_TRUE(take_validation_messages().empty());
}

/// A static ramp at @p angle (about z, rising along +x) and a box resting on it 1 mm above its surface; the box's
/// downhill direction.
struct Ramp
{
    RigidBody* box = nullptr;
    Vector3 downhill{};
};

auto
ramp(Scene& world,
     float angle,
     float friction) -> Ramp
{
    const Quaternion tilt = Quaternion::from_axis_angle({.z = 1}, angle);
    const Vector3 up = gfx::rotate(tilt, {.y = 1});
    world.add({.kind = BodyKind::STATIC, .pose = {.orientation = tilt}}, {.shape = box_shape({.x = 6, .y = 0.25F, .z = 2}), .material = material(friction)});
    RigidBody& box = world.add({.pose = {.position = up * (0.25F + 0.25F + 0.001F), .orientation = tilt}},
                               {.shape = box_shape({.x = 0.25F, .y = 0.25F, .z = 0.25F}), .material = material(friction)});
    return {.box = &box, .downhill = gfx::rotate(tilt, {.x = -1})};
}

TEST_P(GpuXpbdRigidTest, StaticFrictionHoldsABoxOnASlope)
{
    // 20° (tan 0.36) with μ = 0.6: the box stays where it was put (no creep beyond 1 cm in 3 s).
    Scene world = scene();
    const Ramp slope = ramp(world, 20.0F * PI / 180.0F, 0.6F);
    world.run(0.25F);
    const Vector3 start = slope.box->state().pose.position;
    world.run(3.0F);
    const RigidBodyState state = slope.box->state();
    ASSERT_TRUE(finite(state));
    EXPECT_LT(std::abs(gfx::dot(state.pose.position - start, slope.downhill)), 0.01F) << "the box crept down the slope";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, DynamicFrictionSlidesABoxAsCoulombSays)
{
    // 30° with μ = 0.2: the box slides with g (sin θ − μ cos θ) = 3.21 m/s² (within 10 %).
    Scene world = scene();
    const float angle = 30.0F * PI / 180.0F;
    const Ramp slope = ramp(world, angle, 0.2F);
    world.run(0.25F);
    const float first = gfx::dot(slope.box->state().pose.position, slope.downhill);
    world.run(0.75F);
    const float second = gfx::dot(slope.box->state().pose.position, slope.downhill);
    const float expected = STANDARD_GRAVITY * (std::sin(angle) - 0.2F * std::cos(angle)) * 0.5F * (1.0F * 1.0F - 0.25F * 0.25F);
    EXPECT_NEAR(second - first, expected, 0.1F * expected) << "slid " << second - first << " m between 0.25 s and 1 s";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, RestitutionBouncesToTheHeightItPredicts)
{
    // A ball dropped from 2 m: e = 0.5 rebounds e² · 2 = 0.5 m (within 15 %); e = 0 does not rebound.
    for (const float restitution : {0.5F, 0.0F})
    {
        Scene world = scene();
        world.floor(material(0.6F, restitution));
        RigidBody& ball = world.add({.pose = {.position = {.y = 2.5F}}}, {.shape = make_shape(SphereShape{.radius = 0.5F}), .material = material(0.6F, restitution)});
        bool bounced = false;
        float highest = 0.0F;
        for (int step = 0; step < 150; ++step)
        {
            world.world->step(STEP);
            const RigidBodyState state = ball.state();
            ASSERT_TRUE(finite(state));
            bounced = bounced || (state.linear_velocity.y > 0.0F && state.pose.position.y < 0.6F);
            if (bounced)
            {
                highest = std::max(highest, state.pose.position.y - 0.5F);
            }
        }
        if (restitution > 0.0F)
        {
            EXPECT_TRUE(bounced);
            EXPECT_NEAR(highest, restitution * restitution * 2.0F, 0.15F * restitution * restitution * 2.0F) << "rebound height";
        }
        else
        {
            EXPECT_LT(highest, 0.02F) << "a ball without restitution rebounded";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, IslandsFallAsleepAndWakeTogether)
{
    // A stack of three and a lone box fall asleep; a push on the stack's top wakes the stack (its island) within two
    // steps, not the lone box.
    Scene world = scene();
    world.floor();
    const std::vector<RigidBody*> boxes = stack(world, 3, 0.5F);
    RigidBody& lone = world.add({.pose = {.position = {.x = 5, .y = 0.5F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
    world.run(3.0F);
    for (const RigidBody* box : boxes)
    {
        ASSERT_FALSE(box->state().awake);
    }
    ASSERT_FALSE(lone.state().awake);
    boxes.back()->apply_linear_impulse({.x = 0.2F});
    std::uint32_t woken = 0;
    for (int step = 0; step < 2; ++step)
    {
        world.world->step(STEP);
        for (const SleepEvent& event : world.world->events().sleeps)
        {
            woken += event.kind == SleepEventKind::WOKE_UP ? 1U : 0U;
            EXPECT_NE(event.body, lone.handle()) << "the lone box woke";
        }
    }
    for (const RigidBody* box : boxes)
    {
        EXPECT_TRUE(box->state().awake) << "the stack woke as one island";
    }
    EXPECT_EQ(woken, 3U);
    EXPECT_FALSE(lone.state().awake);
    EXPECT_TRUE(take_validation_messages().empty());
}

/// A flat grid of @p cells × @p cells unit cells at y = 0, facing up.
auto
floor_mesh(std::uint32_t cells) -> std::shared_ptr<const Shape>
{
    std::vector<Vector3> vertices;
    for (std::uint32_t row = 0; row <= cells; ++row)
    {
        for (std::uint32_t column = 0; column <= cells; ++column)
        {
            vertices.push_back({.x = static_cast<float>(column) - static_cast<float>(cells) * 0.5F, .z = static_cast<float>(row) - static_cast<float>(cells) * 0.5F});
        }
    }
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::uint32_t row = 0; row < cells; ++row)
    {
        for (std::uint32_t column = 0; column < cells; ++column)
        {
            const std::uint32_t corner = row * (cells + 1) + column;
            triangles.push_back({corner, corner + cells + 1, corner + cells + 2});
            triangles.push_back({corner, corner + cells + 2, corner + 1});
        }
    }
    return make_shape(TriangleMeshShape{{.vertices = vertices, .triangles = triangles}});
}

/// The lowest point of @p shape at @p pose (a compound's from its children: its own box is a conservative one).
auto
lowest_point(const Shape& shape,
             const Transform& pose) -> float
{
    if (const auto* compound = shape.get_if<CompoundShape>())
    {
        float lowest = std::numeric_limits<float>::infinity();
        for (const CompoundChild& child : compound->children())
        {
            lowest = std::min(lowest, lowest_point(*child.shape, pose * child.pose));
        }
        return lowest;
    }
    return shape.bounds(pose).minimum.y;
}

TEST_P(GpuXpbdRigidTest, EveryShapeComesToRestOnMeshesAndHeightFields)
{
    // Sphere, capsule, cylinder, cone, box, rounded box, hull and compound (rolling resistance 2 cm) dropped on a mesh floor
    // and on a flat height field: each comes to rest on it (its lowest point within 2 cm of the surface, slower than
    // 0.1 m/s).
    const std::vector<Vector3> corners{{.x = -0.4F, .y = -0.3F, .z = -0.4F}, {.x = 0.4F, .y = -0.3F, .z = -0.4F}, {.x = 0.0F, .y = -0.3F, .z = 0.5F},
                                       {.x = 0.0F, .y = 0.4F, .z = 0.0F}};
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.25F});
    const std::vector<std::shared_ptr<const Shape>> shapes{
        make_shape(SphereShape{.radius = 0.4F}),
        make_shape(CapsuleShape{.half_height = 0.4F, .radius = 0.25F}),
        make_shape(CylinderShape{.half_height = 0.35F, .radius = 0.35F}),
        make_shape(ConeShape{.half_height = 0.4F, .radius = 0.4F}),
        box_shape({.x = 0.4F, .y = 0.3F, .z = 0.5F}),
        box_shape({.x = 0.35F, .y = 0.25F, .z = 0.45F}, 0.05F),
        make_shape(HullShape::from_points(corners)),
        make_shape(CompoundShape{{CompoundChild{.shape = ball, .pose = {.position = {.x = -0.4F}}}, CompoundChild{.shape = ball, .pose = {.position = {.x = 0.4F}}},
                                  CompoundChild{.shape = box_shape({.x = 0.35F, .y = 0.08F, .z = 0.08F})}}}),
    };
    std::vector<float> samples(21 * 21, 0.0F);
    const std::shared_ptr<const Shape> field =
        make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{samples.data(), 21, 21}}});
    for (const bool mesh : {true, false})
    {
        Scene world = scene();
        world.add({.kind = BodyKind::STATIC, .pose = {.position = mesh ? Vector3{} : Vector3{.x = -10, .z = -10}}}, {.shape = mesh ? floor_mesh(20) : field});
        std::vector<RigidBody*> bodies;
        for (std::size_t index = 0; index < shapes.size(); ++index)
        {
            const Vector3 position{.x = (static_cast<float>(index % 4) - 1.5F) * 2.0F, .y = 1.5F, .z = (static_cast<float>(index / 4) - 0.5F) * 2.0F};
            bodies.push_back(&world.add({.pose = {.position = position, .orientation = Quaternion::from_axis_angle({.z = 1}, 0.3F * static_cast<float>(index))}},
                                        {.shape = shapes[index], .material = {.friction = {.rolling = 0.02F}}}));
        }
        world.run(4.0F);
        for (std::size_t index = 0; index < bodies.size(); ++index)
        {
            const RigidBodyState state = bodies[index]->state();
            ASSERT_TRUE(finite(state));
            const float lowest = lowest_point(*shapes[index], state.pose);
            EXPECT_GT(lowest, -0.02F) << (mesh ? "mesh" : "height field") << ", shape " << index << " sank";
            EXPECT_LT(lowest, 0.05F) << (mesh ? "mesh" : "height field") << ", shape " << index << " does not rest on the floor";
            EXPECT_LT(gfx::length(state.linear_velocity), 0.1F) << (mesh ? "mesh" : "height field") << ", shape " << index;
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

/// The states of @p bodies, as bytes.
auto
state_bytes(const std::vector<RigidBody*>& bodies) -> std::vector<std::byte>
{
    std::vector<std::byte> bytes;
    for (const RigidBody* body : bodies)
    {
        const RigidBodyState state = body->state();
        for (const float value : {state.pose.position.x, state.pose.position.y, state.pose.position.z, state.pose.orientation.x, state.pose.orientation.y,
                                  state.pose.orientation.z, state.pose.orientation.w, state.linear_velocity.x, state.linear_velocity.y, state.linear_velocity.z,
                                  state.angular_velocity.x, state.angular_velocity.y, state.angular_velocity.z})
        {
            const auto value_bytes = std::bit_cast<std::array<std::byte, sizeof(float)>>(value);
            bytes.insert(bytes.end(), value_bytes.begin(), value_bytes.end());
        }
    }
    return bytes;
}

/// A pile of @p count mixed bodies dropped from a column (random offsets from @p seed) on a floor.
auto
pile(Scene& world,
     std::uint32_t count,
     std::uint32_t seed) -> std::vector<RigidBody*>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> offset{-1.0F, 1.0F};
    const std::array<std::shared_ptr<const Shape>, 4> shapes{make_shape(SphereShape{.radius = 0.3F}), box_shape({.x = 0.3F, .y = 0.25F, .z = 0.35F}),
                                                             make_shape(CapsuleShape{.half_height = 0.3F, .radius = 0.2F}),
                                                             make_shape(CylinderShape{.half_height = 0.25F, .radius = 0.3F})};
    std::vector<RigidBody*> bodies;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const Vector3 position{.x = offset(generator), .y = 0.5F + 0.7F * static_cast<float>(index), .z = offset(generator)};
        bodies.push_back(&world.add({.pose = {.position = position, .orientation = Quaternion::from_axis_angle(gfx::normalized(Vector3{.x = offset(generator), .y = 1, .z = offset(generator)}), offset(generator))}},
                                    {.shape = shapes[index % shapes.size()]}));
    }
    return bodies;
}

TEST_P(GpuXpbdRigidTest, TheSameStepsGiveTheSameBits)
{
    // Two worlds, the same pile of 40 bodies: their states equal bit for bit every 30 steps of 4 s.
    Scene first = scene();
    first.floor();
    const std::vector<RigidBody*> first_bodies = pile(first, 40, 7);
    Scene second = scene();
    second.floor();
    const std::vector<RigidBody*> second_bodies = pile(second, 40, 7);
    for (int step = 0; step < 240; ++step)
    {
        first.world->step(STEP);
        second.world->step(STEP);
        if (step % 30 == 29)
        {
            ASSERT_EQ(state_bytes(first_bodies), state_bytes(second_bodies)) << "step " << step;
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, LargePilesOverflowTheFirstBuffersAndAreReplayed)
{
    // 400 bodies dropped at once in a 6 m cube (their contacts far beyond what the first step reserves): after 3 s every
    // one is finite and above the floor, and the steps found contacts.
    Scene world = scene();
    world.floor();
    std::mt19937 generator{11};
    std::uniform_real_distribution<float> coordinate{-3.0F, 3.0F};
    std::vector<RigidBody*> bodies;
    const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.25F});
    for (int index = 0; index < 400; ++index)
    {
        bodies.push_back(&world.add({.pose = {.position = {.x = coordinate(generator), .y = 3.3F + coordinate(generator), .z = coordinate(generator)}}}, {.shape = ball}));
    }
    world.run(3.0F);
    for (const RigidBody* body : bodies)
    {
        const RigidBodyState state = body->state();
        ASSERT_TRUE(finite(state));
        EXPECT_GT(state.pose.position.y, 0.2F) << "a ball sank into the floor";
    }
    EXPECT_GT(world.world->statistics().contacts, 300U);
    EXPECT_TRUE(take_validation_messages().empty());
}

auto
rigid_test_name(const testing::TestParamInfo<std::tuple<std::size_t, GpuSolverMode>>& parameter) -> std::string
{
    return std::format("Device{}_{}", std::get<0>(parameter.param), std::get<1>(parameter.param));
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdRigidTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         rigid_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdRigidTest);

// ===================================================================================================== measurements

/// The step time of a pyramid on each GPU (printed).
class GpuXpbdRigidMeasurementTest : public GpuXpbdRigidTest
{
};

TEST_P(GpuXpbdRigidMeasurementTest, StepTimeIsMeasured)
{
    // A pyramid of 210 boxes (20 at its base) settling, sleep off: the mean step time over 60 steps after 30.
    Scene world = scene({.sleep = {.enabled = false}});
    world.floor();
    constexpr std::uint32_t BASE = 20;
    for (std::uint32_t row = 0; row < BASE; ++row)
    {
        for (std::uint32_t column = 0; column < BASE - row; ++column)
        {
            (void)world.add({.pose = {.position = {.x = (static_cast<float>(column) - static_cast<float>(BASE - row - 1) * 0.5F) * 1.05F,
                                                   .y = 0.5F + static_cast<float>(row) * 1.001F}}},
                            {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
        }
    }
    world.run(0.5F);
    const auto start = std::chrono::steady_clock::now();
    world.run(1.0F);
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60.0;
    std::cout << std::format("{} | {} | 210 boxes in a pyramid: {:.2f} ms per step ({} contact points)\n", m_physical_device->properties().name, mode(),
                             milliseconds, world.world->statistics().contacts);
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdRigidMeasurementTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         rigid_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdRigidMeasurementTest);

} // namespace
