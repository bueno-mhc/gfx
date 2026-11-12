// The GPU backend's rigid bodies in contact (phase G5), on each GPU of the machine with the Vulkan validation layers, in
// both solver modes (graph coloring, Jacobi): a box coming to rest on a floor, stacks and pyramids standing still, static
// friction holding a box on a slope and dynamic friction letting one slide as Coulomb's law says, restitution bouncing a
// ball to the height e² predicts, islands falling asleep together and waking together, every shape kind coming to rest on
// a triangle mesh and a height field, the same steps giving the same bits, and a pile large enough to overflow the first
// step's buffers (replayed).
#include "vulkan_test.hpp"

import gfx.physics;
import gfx.physics.gpu_xpbd;
import gfx.profiler;

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
    return gfx::tests::usable_physical_devices();
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

    [[nodiscard]] auto scene(const PhysicsWorldDescription& description = {},
                             GpuOptimization optimizations = DEFAULT_GPU_OPTIMIZATIONS) -> Scene
    {
        return Scene{.world = gpu::create_physics_world(description, *m_device, {.solver_mode = mode(), .optimizations = optimizations})};
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

TEST_P(GpuXpbdRigidTest, HeavyNodesGatherInChunksAsTheGraphGathers)
{
    // A plate carrying 100 balls: more contacts on one node than colors, so its overflow constraints are gathered in chunks
    // (CHUNKED_GATHER, from the second step): the same motion as the graph's gather within 0.1 mm over 1 s, the same bits
    // when run again, and chunks counted (builds with the profiler). The coloring by the bodies' maxima
    // (NODE_MAXIMA_COLORING) gives the scan's colors: the same bits without it.
    const auto build = [](Scene& world) {
        world.floor();
        std::vector<RigidBody*> bodies;
        bodies.push_back(&world.add({.pose = {.position = {.y = 0.1F}}}, {.shape = box_shape({.x = 1.2F, .y = 0.1F, .z = 1.2F})}));
        const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.08F});
        for (int row = 0; row < 10; ++row)
        {
            for (int column = 0; column < 10; ++column)
            {
                const Vector3 position{.x = -0.99F + 0.22F * static_cast<float>(column), .y = 0.281F, .z = -0.99F + 0.22F * static_cast<float>(row)};
                bodies.push_back(&world.add({.pose = {.position = position}}, {.shape = ball}));
            }
        }
        return bodies;
    };
    const GpuOptimization chunked = DEFAULT_GPU_OPTIMIZATIONS;
    const auto graph = static_cast<GpuOptimization>(std::to_underlying(chunked) & ~std::to_underlying(GpuOptimization::CHUNKED_GATHER));
    const auto scanned = static_cast<GpuOptimization>(std::to_underlying(chunked) & ~std::to_underlying(GpuOptimization::NODE_MAXIMA_COLORING));
    std::optional<double> chunks;
    const auto run = [&](GpuOptimization optimizations, bool trace) {
        Scene world = scene({}, optimizations);
        const std::vector<RigidBody*> bodies = build(world);
        const std::unique_ptr<gfx::profiler::Profiler> recorder = trace ? gfx::profiler::create_profiler() : nullptr;
        gfx::profiler::SessionHandle session{};
        if (recorder != nullptr)
        {
            gfx::profiler::set_current_profiler(recorder.get());
            session = recorder->create_session({.name = "chunked gather"});
        }
        for (int step = 0; step < 60; ++step)
        {
            if (recorder != nullptr)
            {
                recorder->begin_frame(session);
            }
            world.world->step(STEP);
            if (recorder != nullptr)
            {
                (void)recorder->end_frame(session);
            }
        }
        if (recorder != nullptr)
        {
            const gfx::profiler::Trace trace_made = recorder->finalize_session(session);
            gfx::profiler::set_current_profiler(nullptr);
            for (const gfx::profiler::TraceFrame& frame : trace_made.frames())
            {
                for (const gfx::profiler::TraceSample& sample : frame.samples)
                {
                    if (trace_made.sites()[sample.site].name == "GPU XPBD: gather chunks")
                    {
                        chunks = std::max(chunks.value_or(0.0), sample.number());
                    }
                }
            }
        }
        std::vector<RigidBodyState> states;
        for (const RigidBody* body : bodies)
        {
            states.push_back(body->state());
        }
        return std::pair{states, state_bytes(bodies)};
    };
    const auto [chunked_states, chunked_bits] = run(chunked, true);
    const auto [again_states, again_bits] = run(chunked, false);
    const auto [graph_states, graph_bits] = run(graph, false);
    const auto [scanned_states, scanned_bits] = run(scanned, false);
    EXPECT_EQ(chunked_bits, again_bits) << "the same steps, the same bits";
    EXPECT_EQ(chunked_bits, scanned_bits) << "the bodies' maxima color as the scan colors";
    float largest = 0.0F;
    for (std::size_t index = 0; index < chunked_states.size(); ++index)
    {
        ASSERT_TRUE(finite(chunked_states[index]));
        largest = std::max(largest, gfx::length(chunked_states[index].pose.position - graph_states[index].pose.position));
    }
    EXPECT_LT(largest, 1.0e-4F) << "chunked sums move the bodies as the graph's";
    EXPECT_NEAR(chunked_states[0].pose.position.y, 0.1F, 0.01F) << "the plate stays on the floor";
    if constexpr (gfx::profiler::PROFILER_ENABLED)
    {
        ASSERT_TRUE(chunks.has_value());
        EXPECT_GT(*chunks, 0.0) << "the plate's overflow constraints were gathered in chunks";
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

/// @p optimizations without @p removed.
[[nodiscard]] auto
without(GpuOptimization optimizations,
        GpuOptimization removed) -> GpuOptimization
{
    return static_cast<GpuOptimization>(std::to_underlying(optimizations) & ~std::to_underlying(removed));
}

TEST_P(GpuXpbdRigidTest, PassesRecordedForWhatTheLastStepUsedGiveTheSameBits)
{
    // RECORDED_PASSES_BY_USE: a plate carrying 23 balls (24 contacts on one body: the coloring's 24 rounds color them all,
    // one a round — 24 contact colors, no overflow set: 26 colors recorded, no overflow passes), then 11 more landing on
    // it (35 contacts: 24 colored, 11 in an overflow set, no new color: the step replayed for the overflow passes alone)
    // give the bits of passes recorded for at least GPU_XPBD_INITIAL_COLORS colors and the overflow set every step; a
    // chain of joints beside it has joint colors of its own.
    const auto run = [&](GpuOptimization optimizations) {
        Scene world = scene({}, optimizations);
        world.floor();
        std::vector<RigidBody*> bodies;
        bodies.push_back(&world.add({.pose = {.position = {.y = 0.1F}}}, {.shape = box_shape({.x = 1.2F, .y = 0.1F, .z = 1.2F})}));
        const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.08F});
        for (int index = 0; index < 34; ++index)
        {
            const int place = index < 23 ? index : 50 + index;
            const Vector3 position{.x = -0.99F + 0.22F * static_cast<float>(place % 10), .y = index < 23 ? 0.281F : 1.2F,
                                   .z = -0.99F + 0.22F * static_cast<float>(place / 10)};
            bodies.push_back(&world.add({.pose = {.position = position}}, {.shape = ball}));
        }
        std::vector<std::unique_ptr<Joint>> joints;
        const std::shared_ptr<const Shape> link = box_shape({.x = 0.05F, .y = 0.05F, .z = 0.2F});
        RigidBody* previous = nullptr;
        for (int index = 0; index < 8; ++index)
        {
            RigidBody& linked = world.add({.pose = {.position = {.x = 3.0F, .y = 2.0F, .z = 0.4F * static_cast<float>(index)}}}, {.shape = link});
            bodies.push_back(&linked);
            joints.push_back(world.world->create_joint(ball_and_socket_joint({.first = linked.handle(),
                                                                              .second = previous != nullptr ? previous->handle() : RigidBodyHandle{},
                                                                              .anchor = {.x = 3.0F, .y = 2.0F, .z = 0.4F * static_cast<float>(index) - 0.2F}})));
            previous = &linked;
        }
        world.run(1.0F);
        std::vector<std::byte> bits = state_bytes(bodies);
        joints.clear();
        return bits;
    };
    const std::vector<std::byte> by_use = run(DEFAULT_GPU_OPTIMIZATIONS);
    const std::vector<std::byte> every = run(without(DEFAULT_GPU_OPTIMIZATIONS, GpuOptimization::RECORDED_PASSES_BY_USE));
    EXPECT_EQ(by_use, every) << "passes recorded for what the last step used give the same bits";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, ReachBoxesFindThePairsTheFilterKeeps)
{
    // REACH_BOXES: a grid of resting boxes 5 cm apart and a ball fired across them at 40 m/s (the step's speculative
    // distance grows by its 0.67 m a step): each step keeps the same pairs as the common boxes' trees (the steps' counts),
    // the bodies end where they end with them (within 0.1 mm), and the trees find far fewer pairs (traced builds).
    const auto run = [&](GpuOptimization optimizations, std::vector<std::uint32_t>& pairs, double& tree_pairs) {
        Scene world = scene({}, optimizations);
        world.floor();
        std::vector<RigidBody*> bodies;
        const std::shared_ptr<const Shape> box = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F});
        for (int row = 0; row < 12; ++row)
        {
            for (int column = 0; column < 12; ++column)
            {
                const Vector3 position{.x = 0.25F * static_cast<float>(column), .y = 0.1F, .z = 0.25F * static_cast<float>(row)};
                bodies.push_back(&world.add({.pose = {.position = position}}, {.shape = box}));
            }
        }
        bodies.push_back(&world.add({.pose = {.position = {.x = -3.0F, .y = 1.5F, .z = 1.4F}}, .linear_velocity = {.x = 40.0F}},
                                    {.shape = make_shape(SphereShape{.radius = 0.1F})}));
        const std::unique_ptr<gfx::profiler::Profiler> recorder = gfx::profiler::create_profiler();
        gfx::profiler::set_current_profiler(recorder.get());
        const gfx::profiler::SessionHandle session = recorder->create_session({.name = "reach boxes"});
        for (int step = 0; step < 30; ++step)
        {
            recorder->begin_frame(session);
            world.world->step(STEP);
            (void)recorder->end_frame(session);
            pairs.push_back(world.world->statistics().pairs);
        }
        const gfx::profiler::Trace trace = recorder->finalize_session(session);
        gfx::profiler::set_current_profiler(nullptr);
        for (const gfx::profiler::TraceFrame& frame : trace.frames())
        {
            for (const gfx::profiler::TraceSample& sample : frame.samples)
            {
                if (trace.sites()[sample.site].name == "GPU XPBD: tree pairs")
                {
                    tree_pairs += sample.number();
                }
            }
        }
        std::vector<RigidBodyState> states;
        for (const RigidBody* body : bodies)
        {
            states.push_back(body->state());
        }
        return states;
    };
    std::vector<std::uint32_t> reach_pairs;
    std::vector<std::uint32_t> common_pairs;
    double reach_tree_pairs = 0.0;
    double common_tree_pairs = 0.0;
    const std::vector<RigidBodyState> reach = run(DEFAULT_GPU_OPTIMIZATIONS, reach_pairs, reach_tree_pairs);
    const std::vector<RigidBodyState> common = run(without(DEFAULT_GPU_OPTIMIZATIONS, GpuOptimization::REACH_BOXES), common_pairs, common_tree_pairs);
    EXPECT_EQ(reach_pairs, common_pairs) << "the steps keep the same pairs";
    float largest = 0.0F;
    for (std::size_t index = 0; index < reach.size(); ++index)
    {
        ASSERT_TRUE(finite(reach[index]));
        largest = std::max(largest, gfx::length(reach[index].pose.position - common[index].pose.position));
    }
    EXPECT_LT(largest, 1.0e-4F) << "the bodies move as with the common boxes";
    EXPECT_GT(reach.back().pose.position.x, 5.0F) << "the ball crossed the grid";
    if constexpr (gfx::profiler::PROFILER_ENABLED)
    {
        EXPECT_LT(4.0 * reach_tree_pairs, common_tree_pairs) << "the trees found " << reach_tree_pairs << " pairs, against " << common_tree_pairs;
    }
    EXPECT_TRUE(take_validation_messages().empty());
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

/// The step's events, as text (kinds, handles, values' bits).
auto
event_text(const PhysicsWorld& world) -> std::string
{
    std::string text;
    const PhysicsEvents events = world.events();
    for (const SleepEvent& event : events.sleeps)
    {
        text += std::format("sleep {} {}; ", std::to_underlying(event.kind), event.body);
    }
    for (const ContactEvent& event : events.contacts)
    {
        text += std::format("contact {} {} {} {:a} {:a} {:a}; ", std::to_underlying(event.kind), event.first, event.second, event.point.x, event.point.y,
                            event.approach_speed);
    }
    return text;
}

TEST_P(GpuXpbdRigidTest, CompactReadbackGivesTheSameBitsAndEvents)
{
    // COMPACT_READBACK: 4 224 boxes on the floor (a world large enough to list its bodies: GPU_XPBD_COMPACT_READBACK_BODIES)
    // fall asleep while a ball keeps bouncing beside them; then a plank is made just above them and lands on them all at
    // once: about 4 224 bodies wake in one step, after steps listing one or two (more than the step's copy holds: the rest
    // copied after it, as are the contacts' reports; then every body is read back while most of them move). Every step's
    // states and events are those of reading back every body.
    constexpr int BOXES = 66 * 64;
    static_assert(BOXES >= static_cast<int>(GPU_XPBD_COMPACT_READBACK_BODIES));
    const auto run = [&](GpuOptimization optimizations) {
        Scene world = scene({}, optimizations);
        world.floor();
        std::vector<RigidBody*> bodies;
        const std::shared_ptr<const Shape> box = box_shape({.x = 0.15F, .y = 0.15F, .z = 0.15F});
        for (int index = 0; index < BOXES; ++index)
        {
            bodies.push_back(&world.add({.pose = {.position = {.x = 0.4F * static_cast<float>(index % 66) - 13.0F, .y = 0.15F, .z = 0.4F * static_cast<float>(index / 66) - 12.6F}}},
                                        {.shape = box}));
        }
        bodies.push_back(&world.add({.pose = {.position = {.x = 16.0F, .y = 2.0F}}, .linear_velocity = {.y = -3.0F}},
                                    {.shape = make_shape(SphereShape{.radius = 0.2F}), .material = {.restitution = 0.9F}}));
        std::vector<std::vector<std::byte>> states;
        std::string events;
        std::uint32_t most_woken = 0;
        const auto step_once = [&] {
            world.world->step(STEP);
            states.push_back(state_bytes(bodies));
            events += std::format("[{}] {}", states.size(), event_text(*world.world));
            std::uint32_t woken = 0;
            for (const SleepEvent& event : world.world->events().sleeps)
            {
                woken += event.kind == SleepEventKind::WOKE_UP ? 1U : 0U;
            }
            most_woken = std::max(most_woken, woken);
        };
        // Until the boxes sleep (the same step in both runs: the same bits), 10 steps more (the step they fell asleep in
        // listed them all: its successor copies as many), then the plank, then 30 steps.
        const auto boxes_asleep = [&] { return std::ranges::none_of(bodies.begin(), bodies.begin() + BOXES, [](const RigidBody* body) { return body->state().awake; }); };
        for (int step = 0; step < 180 && !boxes_asleep(); ++step)
        {
            step_once();
        }
        const bool slept = boxes_asleep();
        for (int step = 0; step < 10; ++step)
        {
            step_once();
        }
        bodies.push_back(&world.add({.pose = {.position = {.y = 0.36F}}, .linear_velocity = {.y = -1.0F}},
                                    {.shape = box_shape({.x = 13.4F, .y = 0.05F, .z = 13.0F})}));
        const std::unique_ptr<gfx::profiler::Profiler> recorder = gfx::profiler::create_profiler();
        gfx::profiler::set_current_profiler(recorder.get());
        const gfx::profiler::SessionHandle session = recorder->create_session({.name = "compact readback"});
        for (int step = 0; step < 30; ++step)
        {
            recorder->begin_frame(session);
            step_once();
            (void)recorder->end_frame(session);
        }
        const gfx::profiler::Trace trace = recorder->finalize_session(session);
        gfx::profiler::set_current_profiler(nullptr);
        int read_again = 0;
        for (const gfx::profiler::TraceFrame& frame : trace.frames())
        {
            for (const gfx::profiler::TraceScope& scope : frame.scopes)
            {
                read_again += trace.sites()[scope.site].name == "GPU XPBD: read the bodies again" ? 1 : 0;
            }
        }
        return std::tuple{states, events, slept, most_woken, read_again};
    };
    const auto [compact_states, compact_events, compact_slept, compact_woken, compact_read_again] = run(DEFAULT_GPU_OPTIMIZATIONS);
    const auto [full_states, full_events, full_slept, full_woken, full_read_again] =
        run(without(DEFAULT_GPU_OPTIMIZATIONS, GpuOptimization::COMPACT_READBACK));
    ASSERT_EQ(compact_states.size(), full_states.size());
    for (std::size_t step = 0; step < compact_states.size(); ++step)
    {
        ASSERT_EQ(compact_states[step], full_states[step]) << "step " << step;
    }
    EXPECT_EQ(compact_events, full_events);
    EXPECT_TRUE(compact_slept) << "the boxes fell asleep";
    EXPECT_GT(compact_woken, static_cast<std::uint32_t>(BOXES - 100)) << "the plank woke them at once";
    if constexpr (gfx::profiler::PROFILER_ENABLED)
    {
        EXPECT_GE(compact_read_again, 1) << "more bodies were listed than the step copied: the rest read after it";
        EXPECT_EQ(full_read_again, 0);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, RestingWorldsStepWithoutTheGpu)
{
    // RESTING_STEPS: 30 boxes on the floor fall asleep; then steps change nothing (the same bits, the same contacts; no GPU
    // step: their scope counted in builds with the profiler) until an edge box is pushed outward: it slides about 0.4 m and
    // comes to rest again. It ends where it ends when every step runs on the GPU (within 1 cm: its contacts' warm start
    // comes from the last step that ran).
    const auto run = [&](GpuOptimization optimizations, double& resting_steps) {
        Scene world = scene({}, optimizations);
        world.floor();
        std::vector<RigidBody*> boxes;
        const std::shared_ptr<const Shape> box = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F});
        for (int index = 0; index < 30; ++index)
        {
            boxes.push_back(&world.add({.pose = {.position = {.x = 0.6F * static_cast<float>(index % 10), .y = 0.2F, .z = 0.6F * static_cast<float>(index / 10)}}},
                                       {.shape = box}));
        }
        world.run(1.2F);
        for (const RigidBody* body : boxes)
        {
            EXPECT_FALSE(body->state().awake) << "the boxes rest";
        }
        const std::vector<std::byte> rested = state_bytes(boxes);
        const std::unique_ptr<gfx::profiler::Profiler> recorder = gfx::profiler::create_profiler();
        gfx::profiler::set_current_profiler(recorder.get());
        const gfx::profiler::SessionHandle session = recorder->create_session({.name = "resting steps"});
        for (int step = 0; step < 30; ++step)
        {
            recorder->begin_frame(session);
            world.world->step(STEP);
            (void)recorder->end_frame(session);
        }
        const gfx::profiler::Trace trace = recorder->finalize_session(session);
        gfx::profiler::set_current_profiler(nullptr);
        for (const gfx::profiler::TraceFrame& frame : trace.frames())
        {
            for (const gfx::profiler::TraceScope& scope : frame.scopes)
            {
                resting_steps += trace.sites()[scope.site].name == "GPU XPBD: a resting step" ? 1.0 : 0.0;
            }
        }
        EXPECT_EQ(state_bytes(boxes), rested) << "a resting world stays as it is";
        std::array<ContactInfo, 8> contacts{};
        const QueryResult<std::span<ContactInfo>> found = world.world->contacts_of(boxes[12]->handle(), contacts);
        EXPECT_TRUE(found.has_value() && !found->empty()) << "a resting box still touches the floor";
        boxes[9]->apply_linear_impulse({.x = 150.0F});
        world.run(2.0F);
        EXPECT_FALSE(boxes[9]->state().awake) << "the pushed box rests again";
        return boxes[9]->state().pose.position;
    };
    double resting_steps = 0.0;
    double resting_steps_without = 0.0;
    const Vector3 resting = run(DEFAULT_GPU_OPTIMIZATIONS, resting_steps);
    const Vector3 every = run(without(DEFAULT_GPU_OPTIMIZATIONS, GpuOptimization::RESTING_STEPS), resting_steps_without);
    EXPECT_GT(resting.x, 5.6F) << "the pushed box slid";
    EXPECT_LT(gfx::length(resting - every), 0.01F) << "the pushed box ends where it ends with every step on the GPU";
    if constexpr (gfx::profiler::PROFILER_ENABLED)
    {
        EXPECT_EQ(resting_steps, 30.0) << "the resting world's steps ran without the GPU";
        EXPECT_EQ(resting_steps_without, 0.0);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdRigidTest, RefitTreesKeepEveryContact)
{
    // BVH_REFIT: 64 balls dropped in a jittered grid pile up on the floor (the moving tree refit between builds every
    // GPU_XPBD_REFIT_STEPS steps): no ball sinks into another or into the floor more than with a tree built every step
    // allows (a refit that missed pairs lets balls pass through each other); refits and builds counted (traced builds).
    const auto run = [&](GpuOptimization optimizations, double& refits, double& builds) {
        Scene world = scene({}, optimizations);
        world.floor();
        std::mt19937 generator{5};
        std::uniform_real_distribution<float> jitter{-0.05F, 0.05F};
        std::vector<RigidBody*> balls;
        const std::shared_ptr<const Shape> ball = make_shape(SphereShape{.radius = 0.15F});
        for (int index = 0; index < 64; ++index)
        {
            const Vector3 position{.x = 0.35F * static_cast<float>(index % 4) + jitter(generator), .y = 1.0F + 0.35F * static_cast<float>(index / 16),
                                   .z = 0.35F * static_cast<float>((index / 4) % 4) + jitter(generator)};
            balls.push_back(&world.add({.pose = {.position = position}}, {.shape = ball}));
        }
        const std::unique_ptr<gfx::profiler::Profiler> recorder = gfx::profiler::create_profiler();
        gfx::profiler::set_current_profiler(recorder.get());
        const gfx::profiler::SessionHandle session = recorder->create_session({.name = "refit"});
        for (int step = 0; step < 90; ++step)
        {
            recorder->begin_frame(session);
            world.world->step(STEP);
            (void)recorder->end_frame(session);
        }
        const gfx::profiler::Trace trace = recorder->finalize_session(session);
        gfx::profiler::set_current_profiler(nullptr);
        for (const gfx::profiler::TraceFrame& frame : trace.frames())
        {
            for (const gfx::profiler::TraceSample& sample : frame.samples)
            {
                if (trace.sites()[sample.site].name == "GPU XPBD: moving tree refit")
                {
                    (sample.number() > 0.0 ? refits : builds) += 1.0;
                }
            }
        }
        float deepest = 0.0F;
        for (std::size_t first = 0; first < balls.size(); ++first)
        {
            const Vector3 position = balls[first]->state().pose.position;
            EXPECT_TRUE(finite(balls[first]->state()));
            deepest = std::max(deepest, 0.15F - position.y);
            for (std::size_t second = first + 1; second < balls.size(); ++second)
            {
                deepest = std::max(deepest, 0.3F - gfx::length(position - balls[second]->state().pose.position));
            }
        }
        return deepest;
    };
    double refits = 0.0;
    double builds = 0.0;
    double unused_refits = 0.0;
    double unused_builds = 0.0;
    const float refit_depth = run(DEFAULT_GPU_OPTIMIZATIONS, refits, builds);
    const float built_depth = run(without(DEFAULT_GPU_OPTIMIZATIONS, GpuOptimization::BVH_REFIT), unused_refits, unused_builds);
    EXPECT_LT(refit_depth, std::max(0.02F, 2.0F * built_depth)) << "the refit trees found every contact (built every step: " << built_depth << " m)";
    if constexpr (gfx::profiler::PROFILER_ENABLED)
    {
        EXPECT_GE(refits, 80.0) << "the tree was refit between builds";
        EXPECT_GE(builds, 3.0) << "and built every GPU_XPBD_REFIT_STEPS steps";
        EXPECT_EQ(unused_refits, 0.0);
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
