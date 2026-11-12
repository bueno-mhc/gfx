// The GPU backend's soft bodies (phase G7), on each GPU of the machine with the Vulkan validation layers, in both solver
// modes (graph coloring, Jacobi): the abstraction's soft-body scenarios in GPU worlds — a rope keeps its length, a cloth
// hanging from two pins settles, a bent hinge returns to its rest angle, a volume rests on a floor with its volume,
// particles rest on every kind of shape, friction holds on a slope steeper than its angle only, self-collision keeps
// particles apart, momentum is exchanged with a dynamic body — then the Box3D scenarios of soft bodies with rigid ones (a
// rope holding a body up and letting it fall when unpinned, a cloth falling onto colliders and resting), edits reaching
// the particles, and the same steps giving the same bits.
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
    return gfx::tests::usable_physical_devices();
}

auto
box_shape(Vector3 half) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half});
}

/// A world with its bodies, colliders and soft bodies (the soft bodies destroyed first, then the colliders, then the
/// bodies, then the world).
struct Scene
{
    std::unique_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<RigidBody>> bodies{};
    std::vector<std::unique_ptr<Collider>> colliders{};
    std::vector<std::unique_ptr<SoftBody>> soft_bodies{};

    Scene(std::unique_ptr<PhysicsWorld> made)
        : world{std::move(made)}
    {
    }

    Scene(Scene&&) = default;

    ~Scene()
    {
        soft_bodies.clear();
        colliders.clear();
        bodies.clear();
    }

    auto add(const RigidBodyDescription& body,
             const ColliderDescription& collider) -> RigidBody&
    {
        bodies.push_back(world->create_rigid_body(body));
        colliders.push_back(bodies.back()->add_collider(collider));
        return *bodies.back();
    }

    /// A static body at @p pose with one collider of @p shape.
    auto add_static(std::shared_ptr<const Shape> shape,
                    const Transform& pose,
                    SurfaceMaterial material = {}) -> RigidBody&
    {
        return add({.kind = BodyKind::STATIC, .pose = pose}, {.shape = std::move(shape), .material = material});
    }

    /// A static floor, its top at y = 0.
    auto floor() -> RigidBody&
    {
        return add_static(box_shape({.x = 20, .y = 0.5F, .z = 20}), {.position = {.y = -0.5F}});
    }

    auto soft(const SoftBodyDescription& description) -> SoftBody&
    {
        soft_bodies.push_back(world->create_soft_body(description));
        return *soft_bodies.back();
    }

    void run(std::uint32_t steps)
    {
        for (std::uint32_t step = 0; step < steps; ++step)
        {
            world->step(STEP);
        }
    }
};

auto
positions_of(const SoftBody& soft_body) -> std::vector<Vector3>
{
    std::vector<Vector3> positions(soft_body.particle_count());
    (void)soft_body.positions(positions);
    return positions;
}

auto
velocities_of(const SoftBody& soft_body) -> std::vector<Vector3>
{
    std::vector<Vector3> velocities(soft_body.particle_count());
    (void)soft_body.velocities(velocities);
    return velocities;
}

/// The largest speed of a particle of @p soft_body.
auto
fastest(const SoftBody& soft_body) -> float
{
    float largest = 0.0F;
    for (const Vector3 velocity : velocities_of(soft_body))
    {
        largest = std::max(largest, gfx::length(velocity));
    }
    return largest;
}

/// The largest relative stretch of the STRETCH constraints of @p description at @p positions.
auto
largest_stretch(const SoftBodyDescription& description,
                const std::vector<Vector3>& positions) -> float
{
    const SoftBodyModel model{description};
    float largest = 0.0F;
    for (const SoftBodyModel::Constraint& constraint : model.constraints())
    {
        if (constraint.kind == SoftBodyConstraintKind::STRETCH)
        {
            largest = std::max(largest, gfx::distance(positions[constraint.particles[0]], positions[constraint.particles[1]]) / constraint.rest - 1.0F);
        }
    }
    return largest;
}

auto
finite(Vector3 value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// ===================================================================================================== fixture

/// A world on each GPU in each solver mode, with the validation layers.
class GpuXpbdSoftBodyTest : public gfx::tests::VulkanDeviceTest,
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
        return Scene{gpu::create_physics_world(description, *m_device, {.solver_mode = mode(), .optimizations = optimizations})};
    }
};

// ===================================================================================================== soft bodies alone

TEST_P(GpuXpbdSoftBodyTest, ARopeHangingFromAPinKeepsItsLength)
{
    // Released horizontally from its pin, it swings down and settles under it; its segments hardly stretch, and not at all
    // beyond the reach of its long-range attachments. (No rigid body in the world.)
    for (const bool tethers : {false, true})
    {
        SCOPED_TRACE(tethers ? "with long-range attachments" : "without");
        Scene world = scene();
        SoftBodyDescription description = rope_preset({.segments = 20, .length = 2.0F, .direction = {.x = 1}, .mass = 0.2F});
        description.pins = {pin_to_world(description, 0)};
        description.damping = 0.8F;
        if (tethers)
        {
            add_long_range_attachments(description);
        }
        SoftBody& rope = world.soft(description);
        float stretch = 0.0F;
        for (std::uint32_t step = 0; step < 300; ++step)
        {
            world.world->step(STEP);
            const std::vector<Vector3> positions = positions_of(rope);
            float length = 0.0F;
            for (std::size_t index = 0; index + 1 < positions.size(); ++index)
            {
                length += gfx::distance(positions[index], positions[index + 1]);
            }
            stretch = std::max(stretch, length / 2.0F - 1.0F);
        }
        const std::vector<Vector3> positions = positions_of(rope);
        EXPECT_LT(stretch, tethers ? 0.01F : 0.03F);
        EXPECT_EQ(positions[0], Vector3{}) << "the pin holds";
        EXPECT_NEAR(positions.back().x, 0.0F, 0.1F) << "under its pin";
        EXPECT_NEAR(positions.back().y, -2.0F, 0.06F);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, AClothHangingFromTwoPinsSettles)
{
    for (const bool dihedral : {false, true})
    {
        SCOPED_TRACE(dihedral ? "dihedral bending" : "distance bending");
        Scene world = scene();
        SoftBodyDescription description = cloth_preset({.columns = 16, .rows = 16, .spacing = 0.05F, .origin = {.y = 2}, .dihedral_bending = dihedral});
        description.pins = {pin_to_world(description, cloth_particle(16, 0, 0)), pin_to_world(description, cloth_particle(16, 15, 0))};
        description.damping = 0.5F;
        SoftBody& cloth = world.soft(description);
        world.run(360);
        const std::vector<Vector3> positions = positions_of(cloth);
        EXPECT_LT(fastest(cloth), 0.02F) << "settled";
        EXPECT_EQ(positions[0], (Vector3{.y = 2})) << "pinned";
        EXPECT_EQ(positions[15], (Vector3{.x = 0.75F, .y = 2}));
        EXPECT_LT(largest_stretch(description, positions), 0.15F);
        EXPECT_LT(positions[cloth_particle(16, 8, 15)].y, 2.0F - 0.7F) << "hanging";
        EXPECT_NEAR(positions[cloth_particle(16, 8, 15)].z, 0.0F, 1.0e-4F) << "in its plane";
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, ABentHingeReturnsToItsRestAngle)
{
    // Two triangles on a fixed edge: one wing lifted out of the plane; the dihedral constraint brings it to the rest angle
    // (0: flat; then 1 radian).
    const auto angle_of = [](const std::vector<Vector3>& positions) {
        const Vector3 edge = positions[1] - positions[0];
        const Vector3 first = gfx::normalized(gfx::cross(positions[2] - positions[0], positions[2] - positions[1]));
        const Vector3 second = gfx::normalized(gfx::cross(positions[3] - positions[1], positions[3] - positions[0]));
        return std::atan2(gfx::dot(gfx::cross(first, second), gfx::normalized(edge)), gfx::dot(first, second));
    };
    for (const float rest : {0.0F, 1.0F})
    {
        SCOPED_TRACE(rest);
        Scene world = scene({.gravity = {}});
        SoftBody& hinge = world.soft({.particles = {{.position = {}, .inverse_mass = 0.0F},
                                                    {.position = {.x = 1}, .inverse_mass = 0.0F},
                                                    {.position = {.x = 0.5F, .y = 1}},
                                                    {.position = {.x = 0.5F, .y = -0.6F, .z = 0.8F}}},
                                      .constraints = {{.kind = SoftBodyConstraintKind::DIHEDRAL, .particles = {0, 1, 2, 3}, .rest_value = rest},
                                                      {.particles = {0, 2}},
                                                      {.particles = {1, 2}},
                                                      {.particles = {0, 3}},
                                                      {.particles = {1, 3}}},
                                      .damping = 0.8F});
        EXPECT_GT(std::abs(angle_of(positions_of(hinge)) - rest), 0.5F);
        world.run(240);
        EXPECT_NEAR(angle_of(positions_of(hinge)), rest, 0.01F);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, AVolumeRestsOnTheFloorAndKeepsItsVolume)
{
    Scene world = scene();
    world.floor();
    const TetrahedralMesh mesh = tetrahedral_box({.half_extents = {.x = 0.2F, .y = 0.2F, .z = 0.2F}, .cells = {3, 3, 3}, .pose = {.position = {.y = 1}}});
    SoftBodyDescription description = volume_preset(mesh);
    description.damping = 0.2F;
    SoftBody& volume = world.soft(description);
    world.run(240);
    const std::vector<Vector3> positions = positions_of(volume);
    // On the floor within the soft contacts' give (contact_hertz 30: each bottom particle holds the particles above it,
    // g / ω² = 0.3 mm per particle's weight).
    const auto lowest = std::ranges::min(positions | std::views::transform(&Vector3::y));
    EXPECT_GT(lowest, description.particle_radius - 4.0e-3F) << "on the floor";
    EXPECT_LT(lowest, description.particle_radius + 1.0e-3F) << "on the floor";
    float total = 0.0F;
    for (const auto& [a, b, c, d] : mesh.tetrahedra)
    {
        total += gfx::dot(gfx::cross(positions[b] - positions[a], positions[c] - positions[a]), positions[d] - positions[a]) / 6.0F;
    }
    EXPECT_NEAR(total, 0.4F * 0.4F * 0.4F, 0.02F * 0.064F);
    EXPECT_LT(fastest(volume), 0.02F);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, ParticlesRestOnEveryKindOfShape)
{
    // A particle dropped from 0.3 above each shape's top comes to rest on it, a radius above (within the soft contacts'
    // give: g / ω² = 0.3 mm at 30 Hz).
    constexpr float RADIUS = 0.05F;
    const std::array<Vector3, 4> corners{Vector3{.x = -1, .z = -1}, Vector3{.x = 1, .z = -1}, Vector3{.x = 1, .z = 1}, Vector3{.x = -1, .z = 1}};
    const std::array<std::array<std::uint32_t, 3>, 2> triangles{{{0, 2, 1}, {0, 3, 2}}};
    const std::array<float, 9> heights{0.2F, 0.2F, 0.2F, 0.2F, 0.2F, 0.2F, 0.2F, 0.2F, 0.2F};
    const std::vector<Vector3> hull_points{{.x = -0.3F, .y = -0.3F, .z = -0.3F}, {.x = 0.3F, .y = -0.3F, .z = -0.3F}, {.x = 0.3F, .y = 0.3F, .z = -0.3F},
                                           {.x = -0.3F, .y = 0.3F, .z = -0.3F}, {.x = -0.3F, .y = -0.3F, .z = 0.3F}, {.x = 0.3F, .y = -0.3F, .z = 0.3F},
                                           {.x = 0.3F, .y = 0.3F, .z = 0.3F},   {.x = -0.3F, .y = 0.3F, .z = 0.3F}};
    struct Case
    {
        const char* name;
        std::shared_ptr<const Shape> shape;
        float top;
        Transform pose{};
    };
    const std::vector<Case> cases{
        {"sphere", make_shape(SphereShape{.radius = 0.5F}), 0.5F},
        {"capsule", make_shape(CapsuleShape{.half_height = 0.4F, .radius = 0.2F}), 0.6F},
        {"box", make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.3F, .z = 0.5F}}), 0.3F},
        {"rounded box", make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.3F, .z = 0.5F}, .radius = 0.1F}), 0.4F},
        {"cylinder", make_shape(CylinderShape{.half_height = 0.3F, .radius = 0.5F}), 0.3F},
        {"cone", make_shape(ConeShape{.half_height = 0.3F, .radius = 0.5F}), 0.3F},
        {"hull", make_shape(HullShape::from_points(hull_points)), 0.3F},
        {"mesh", make_shape(TriangleMeshShape{{.vertices = corners, .triangles = triangles}}), 0.0F},
        {"height field", make_shape(HeightFieldShape{{.heights = std::mdspan<const float, std::dextents<std::size_t, 2>>{heights.data(), 3, 3}}}), 0.2F,
         {.position = {.x = -1, .z = -1}}},
        {"compound",
         make_shape(CompoundShape{{{.shape = make_shape(BoxShape{.half_extents = {.x = 0.5F, .y = 0.1F, .z = 0.5F}}), .pose = {}},
                                   {.shape = make_shape(SphereShape{.radius = 0.2F}), .pose = {.position = {.y = 0.3F}}}}}),
         0.5F},
    };
    for (const Case& shape : cases)
    {
        SCOPED_TRACE(shape.name);
        Scene world = scene();
        world.add_static(shape.shape, shape.pose);
        SoftBody& particle = world.soft({.particles = {{.position = {.y = shape.top + 0.3F}}}, .particle_radius = RADIUS});
        world.run(120);
        const Vector3 position = positions_of(particle)[0];
        EXPECT_NEAR(position.y, shape.top + RADIUS, 1.0e-3F);
        EXPECT_NEAR(position.x, 0.0F, 1.0e-4F);
        EXPECT_LT(fastest(particle), 0.01F);
    }

    // Overlapping a mesh from below: a two-sided one pushes the particle out below; a one-sided one does not touch it (its
    // triangles collide from their front only, as with rigid shapes: the abstraction's soft-body solver would push it out
    // above).
    for (const bool two_sided : {false, true})
    {
        SCOPED_TRACE(two_sided ? "two-sided" : "one-sided");
        Scene world = scene({.gravity = {}});
        world.add_static(make_shape(TriangleMeshShape{{.vertices = corners, .triangles = triangles, .two_sided = two_sided}}), {});
        SoftBody& particle = world.soft({.particles = {{.position = {.y = -0.02F}}}, .particle_radius = RADIUS, .damping = 1.0F});
        world.run(30);
        EXPECT_NEAR(positions_of(particle)[0].y, two_sided ? -RADIUS : -0.02F, 1.0e-3F);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, FrictionHoldsParticlesOnASlopeOnlyAboveItsAngle)
{
    // A slope of 30° (tan 30° = 0.577): friction 0.8 holds a particle, 0.2 lets it slide.
    constexpr float ANGLE = PI / 6.0F;
    const Quaternion tilt = Quaternion::from_axis_angle({.z = 1}, ANGLE);
    const Vector3 up = gfx::rotate(tilt, {.y = 1});
    for (const float friction : {0.8F, 0.2F})
    {
        SCOPED_TRACE(friction);
        Scene world = scene();
        world.add_static(box_shape({.x = 5, .y = 0.5F, .z = 5}), {.position = up * -0.5F, .orientation = tilt},
                         {.friction = {.static_coefficient = friction, .dynamic_coefficient = friction}});
        SoftBody& particle = world.soft({.particles = {{.position = up * 0.05F}}, .particle_radius = 0.05F, .damping = 0.0F, .friction = friction});
        world.run(60);
        const float slid = gfx::length(positions_of(particle)[0] - up * 0.05F);
        if (friction > std::tan(ANGLE))
        {
            EXPECT_LT(slid, 1.0e-3F) << "held";
        }
        else
        {
            // Sliding with friction: a = g (sin θ − μ cos θ), half of it times t² after a second.
            const float expected = 0.5F * STANDARD_GRAVITY * (std::sin(ANGLE) - friction * std::cos(ANGLE));
            EXPECT_NEAR(slid, expected, 0.05F * expected) << "slides";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, SelfCollisionKeepsTheParticlesOfABodyApart)
{
    // Two particles of one body thrown at each other: they pass through each other unless self-collision is on (rigid
    // contacts: contact_hertz 0).
    for (const bool self_collision : {false, true})
    {
        SCOPED_TRACE(self_collision);
        Scene world = scene({.gravity = {}, .solver = {.contact_hertz = 0.0F}});
        SoftBody& pair = world.soft({.particles = {{.position = {.x = -0.5F}, .velocity = {.x = 1}}, {.position = {.x = 0.5F}, .velocity = {.x = -1}}},
                                     .particle_radius = 0.05F,
                                     .damping = 0.0F,
                                     .self_collision = self_collision});
        float closest = 1.0F;
        for (std::uint32_t step = 0; step < 60; ++step)
        {
            world.world->step(STEP);
            const std::vector<Vector3> positions = positions_of(pair);
            closest = std::min(closest, positions[1].x - positions[0].x);
        }
        if (self_collision)
        {
            EXPECT_GT(closest, 0.1F - 2.0e-3F);
            EXPECT_LT(positions_of(pair)[0].x, 0.0F) << "back on its side";
        }
        else
        {
            EXPECT_LT(closest, 0.0F) << "through each other";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

// ===================================================================================================== soft bodies with rigid ones

TEST_P(GpuXpbdSoftBodyTest, MomentumIsExchangedWithADynamicBody)
{
    // No gravity: a soft cube thrown at a 2 kg box floating at rest; the box goes off with the momentum the cube lost.
    Scene world = scene({.gravity = {}});
    RigidBody& target = world.add({.pose = {.position = {.x = 0.8F}}, .allow_sleep = false},
                                  {.shape = box_shape({.x = 0.25F, .y = 0.25F, .z = 0.25F}),
                                   .material = {.friction = {.static_coefficient = 0, .dynamic_coefficient = 0}},
                                   .density = 16.0F});
    SoftBodyDescription description = volume_preset(tetrahedral_box({.half_extents = {.x = 0.15F, .y = 0.15F, .z = 0.15F}, .cells = {2, 2, 2}}), {.density = 200.0F});
    description.damping = 0.0F;
    description.friction = 0.0F;
    float cube_mass = 0.0F;
    for (SoftBodyParticle& particle : description.particles)
    {
        particle.velocity = {.x = 2};
        cube_mass += 1.0F / particle.inverse_mass;
    }
    SoftBody& cube = world.soft(description);
    world.run(60);

    Vector3 momentum = target.state().linear_velocity * target.mass_properties().mass;
    const std::vector<Vector3> velocities = velocities_of(cube);
    Vector3 cube_momentum{};
    for (std::size_t index = 0; index < velocities.size(); ++index)
    {
        cube_momentum += velocities[index] / description.particles[index].inverse_mass;
    }
    momentum += cube_momentum;
    EXPECT_NEAR(target.mass_properties().mass, 2.0F, 1.0e-3F);
    EXPECT_GT(target.state().linear_velocity.x, 0.5F) << "pushed";
    EXPECT_LT(cube_momentum.x, cube_mass * 2.0F * 0.9F) << "slowed";
    EXPECT_NEAR(momentum.x, cube_mass * 2.0F, 1.0e-2F * cube_mass * 2.0F) << "momentum kept";
    EXPECT_NEAR(momentum.y, 0.0F, 1.0e-2F * cube_mass * 2.0F);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, ARopeHoldsABodyUp)
{
    // A 4 kg box hanging from a 1 m rope of 0.2 kg pinned to the world: the rope pulls the box (a pin to a dynamic body
    // shares each correction with it); it stays at the rope's end, at rest. Unpinned, it falls. The world takes the rope's
    // 8 sub-steps as substeps: Gauss-Seidel couples a light rope and a heavy body slowly (each projection moves the body by
    // the mass ratio), and the sag the coupling leaves falls with the substep's square — at the default 4 substeps (the
    // rope's sub-steps then 2 iterations per substep) the box hangs 3.5 cm low, at 8 substeps 1.9 cm.
    Scene world = scene({.solver = {.sub_steps = 8}});
    RigidBody& weight = world.add({.pose = {.position = {.y = 2}}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 500.0F, .filter = {.group = -1}});
    SoftBodyDescription description = rope_preset({.segments = 10, .length = 1.0F, .origin = {.y = 3}, .mass = 0.2F});
    description.pins = {pin_to_world(description, 0), pin_to_body(10, weight.handle(), {})};
    description.filter = {.group = -1};
    description.damping = 0.5F;
    add_long_range_attachments(description);
    SoftBody& rope = world.soft(description);
    world.run(300);
    EXPECT_NEAR(weight.mass_properties().mass, 4.0F, 1.0e-3F);
    EXPECT_NEAR(weight.state().pose.position.y, 2.0F, 0.03F) << "held at the rope's end";
    EXPECT_LT(gfx::length(weight.state().linear_velocity), 0.05F);

    rope.unpin(10);
    world.run(30);
    EXPECT_LT(weight.state().pose.position.y, 1.0F) << "it falls";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, AClothFallsOntoCollidersAndRests)
{
    // A cloth dropped flat over a table on a floor: over the table it rests on it (in folds a few centimeters high at most),
    // never through the table or the floor.
    Scene world = scene();
    world.floor();
    world.add_static(box_shape({.x = 0.3F, .y = 0.3F, .z = 0.3F}), {.position = {.y = 0.3F}});
    SoftBodyDescription description = cloth_preset({.columns = 24, .rows = 24, .spacing = 0.04F, .origin = {.x = -0.46F, .y = 1.0F, .z = -0.46F}, .row_direction = {.z = 1}});
    description.damping = 0.5F;
    SoftBody& cloth = world.soft(description);
    world.run(300);
    const float radius = description.particle_radius;
    for (const Vector3 position : positions_of(cloth))
    {
        ASSERT_TRUE(finite(position));
        EXPECT_GE(position.y, radius - 2.0e-3F);
        if (std::abs(position.x) < 0.28F && std::abs(position.z) < 0.28F)
        {
            EXPECT_GE(position.y, 0.6F + radius - 2.0e-3F) << "not through the table";
            EXPECT_LE(position.y, 0.6F + radius + 0.04F) << "on the table";
        }
    }
    EXPECT_LT(fastest(cloth), 0.05F) << "at rest";
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, AHeavyBoxUnderAClothStaysOnTheFloor)
{
    // A 1 m box of 1 000 kg on the floor with a cloth of 1 600 particles falling onto it (about 400 rest on its top): the
    // box must stay on the floor, the cloth on the box. (The box's particle contacts outnumber the colors: they and, in the
    // Jacobi mode, its floor contact go to the overflow set, whose mass splitting once shared the floor's push among all of
    // them — the box sank through the floor; found by the benchmark's `mixed` scene.)
    Scene world = scene();
    world.floor();
    RigidBody& box = world.add({.pose = {.position = {.y = 0.5F}}}, {.shape = box_shape({.x = 0.5F, .y = 0.5F, .z = 0.5F})});
    SoftBodyDescription description = cloth_preset({.columns = 40, .rows = 40, .spacing = 0.05F, .origin = {.x = -0.975F, .y = 1.3F, .z = -0.975F}, .row_direction = {.z = 1}});
    description.damping = 0.5F;
    SoftBody& cloth = world.soft(description);
    for (int second = 0; second < 3; ++second)
    {
        world.run(60);
        const RigidBodyState state = box.state();
        EXPECT_NEAR(state.pose.position.y, 0.5F, 0.01F) << "after " << second + 1 << " s: on the floor";
        EXPECT_GT(gfx::rotate(state.pose.orientation, Vector3{.y = 1}).y, 0.999F) << "upright";
    }
    const float radius = description.particle_radius;
    for (const Vector3 position : positions_of(cloth))
    {
        ASSERT_TRUE(finite(position));
        if (std::abs(position.x) < 0.45F && std::abs(position.z) < 0.45F)
        {
            EXPECT_GE(position.y, 1.0F + radius - 5.0e-3F) << "on the box, not through it";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, EditsReachTheParticles)
{
    // Without gravity a particle stays; moved, it is where it was put, at rest; with gravity again it falls; with full
    // damping it stops; a pinned particle follows a kinematic body.
    Scene world = scene();
    SoftBody& particle = world.soft({.particles = {{.position = {.y = 5}}}, .damping = 0.0F, .gravity_scale = 0.0F});
    RigidBody& platform = world.add({.kind = BodyKind::KINEMATIC, .pose = {.position = {.x = 3}}}, {.shape = box_shape({.x = 0.2F, .y = 0.2F, .z = 0.2F})});
    SoftBodyDescription held_description{.particles = {{.position = {.x = 3, .y = 0.3F}}}};
    held_description.pins = {pin_to_body(0, platform.handle(), {.y = 0.3F})};
    SoftBody& held = world.soft(held_description);
    world.run(30);
    EXPECT_EQ(positions_of(particle)[0], (Vector3{.y = 5})) << "no gravity";

    particle.move_particle(0, {.x = 1, .y = 4});
    world.run(1);
    EXPECT_EQ(positions_of(particle)[0], (Vector3{.x = 1, .y = 4})) << "moved";
    EXPECT_EQ(velocities_of(particle)[0], Vector3{});

    particle.set_gravity_scale(1.0F);
    world.run(30);
    EXPECT_NEAR(positions_of(particle)[0].y, 4.0F - 0.5F * STANDARD_GRAVITY * 0.25F, 0.05F) << "falling";
    particle.set_damping(1.0F);
    world.run(1);
    EXPECT_LT(gfx::length(velocities_of(particle)[0]), 1.0e-6F) << "damped";

    platform.move_kinematic({.position = {.x = 3, .y = 1}}, 0.5F);
    world.run(30);
    EXPECT_NEAR(positions_of(held)[0].y, platform.state().pose.position.y + 0.3F, 1.0e-4F) << "following its body";
    EXPECT_GT(platform.state().pose.position.y, 0.9F);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, TheSameStepsGiveTheSameBits)
{
    // Two worlds of a cloth falling onto a box and a rope holding a body: the same particles and body, bit for bit.
    const auto run = [this] {
        Scene world = scene();
        world.floor();
        world.add_static(box_shape({.x = 0.3F, .y = 0.3F, .z = 0.3F}), {.position = {.y = 0.3F}});
        RigidBody& weight = world.add({.pose = {.position = {.x = 1, .y = 2}}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F}), .density = 500.0F});
        SoftBodyDescription rope = rope_preset({.segments = 8, .length = 1.0F, .origin = {.x = 1, .y = 3}});
        rope.pins = {pin_to_world(rope, 0), pin_to_body(8, weight.handle(), {})};
        SoftBody& rope_body = world.soft(rope);
        SoftBody& cloth = world.soft(cloth_preset({.columns = 24, .rows = 24, .spacing = 0.04F, .origin = {.x = -0.47F, .y = 1, .z = -0.47F}, .row_direction = {.z = 1}}));
        world.run(60);
        std::vector<Vector3> positions = positions_of(cloth);
        const std::vector<Vector3> rope_positions = positions_of(rope_body);
        positions.insert(positions.end(), rope_positions.begin(), rope_positions.end());
        positions.push_back(weight.state().pose.position);
        return positions;
    };
    const std::vector<Vector3> first = run();
    const std::vector<Vector3> second = run();
    ASSERT_EQ(first.size(), second.size());
    EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size() * sizeof(Vector3)), 0);
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuXpbdSoftBodyTest, FusedVelocityUpdatesGiveTheSameBits)
{
    // KERNEL_FUSION: a cloth falling onto a box beside a falling box, the bodies' and particles' velocity updates in one
    // dispatch: the same particles and bodies, bit for bit, as in two.
    const auto run = [this](GpuOptimization optimizations) {
        Scene world = scene({}, optimizations);
        world.floor();
        world.add_static(box_shape({.x = 0.3F, .y = 0.3F, .z = 0.3F}), {.position = {.y = 0.3F}});
        RigidBody& falling = world.add({.pose = {.position = {.x = 1, .y = 1}}}, {.shape = box_shape({.x = 0.1F, .y = 0.1F, .z = 0.1F})});
        SoftBody& cloth = world.soft(cloth_preset({.columns = 20, .rows = 20, .spacing = 0.04F, .origin = {.x = -0.38F, .y = 1, .z = -0.38F}, .row_direction = {.z = 1}}));
        world.run(60);
        std::vector<Vector3> positions = positions_of(cloth);
        positions.push_back(falling.state().pose.position);
        positions.push_back(falling.state().linear_velocity);
        return positions;
    };
    const std::vector<Vector3> fused = run(DEFAULT_GPU_OPTIMIZATIONS);
    const std::vector<Vector3> separate =
        run(static_cast<GpuOptimization>(std::to_underlying(DEFAULT_GPU_OPTIMIZATIONS) & ~std::to_underlying(GpuOptimization::KERNEL_FUSION)));
    ASSERT_EQ(fused.size(), separate.size());
    EXPECT_EQ(std::memcmp(fused.data(), separate.data(), fused.size() * sizeof(Vector3)), 0);
    EXPECT_LT(fused[fused.size() - 2].y, 0.5F) << "the box fell";
    EXPECT_TRUE(take_validation_messages().empty());
}

/// The volume a closed surface encloses (the divergence theorem).
[[nodiscard]] auto
surface_volume(std::span<const Vector3> positions,
               std::span<const std::array<std::uint32_t, 3>> surface) -> float
{
    float total = 0.0F;
    for (const auto& [a, b, c] : surface)
    {
        total += gfx::dot(positions[a], gfx::cross(positions[b], positions[c])) / 6.0F;
    }
    return total;
}

TEST_P(GpuXpbdSoftBodyTest, AToolTetrahedralizedTorusIsAVolumeOnTheGpu)
{
    // tools/tetrahedralize.py's torus (written by CTest's fixture gfx_tetrahedral_meshes) as a GPU XPBD volume: dropped from
    // 1 m onto the floor, after 1.5 s every particle is finite and above the floor, the torus came down, and its volume is
    // within 5 % of its rest (the volume constraints held).
    const std::filesystem::path directory{GFX_TEST_TETRAHEDRAL_DIRECTORY};
    const auto read_text = [](const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    };
    const std::string node = read_text(directory / "torus.node");
    const std::string element = read_text(directory / "torus.ele");
    if (node.empty() || element.empty())
    {
        GTEST_SKIP() << "no tetrahedralized torus in " << directory.generic_string() << " (CTest's fixture gfx_tetrahedral_meshes writes it: Python 3)";
    }
    TetrahedralMesh mesh = read_tetgen_mesh(node, element);
    const std::vector<std::array<std::uint32_t, 3>> boundary = tetrahedral_mesh_surface(mesh);
    const float rest = surface_volume(mesh.positions, boundary);
    for (Vector3& position : mesh.positions)
    {
        position.y += 1.0F;
    }
    Scene world = scene();
    world.floor();
    SoftBody& torus = world.soft(volume_preset(mesh, {.density = 300.0F}));
    world.run(90);
    std::vector<Vector3> positions(torus.particle_count());
    (void)torus.positions(positions);
    for (const Vector3 position : positions)
    {
        ASSERT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
        EXPECT_GT(position.y, -0.03F) << "a particle went through the floor";
    }
    EXPECT_LT(std::ranges::max(positions, {}, &Vector3::y).y, 0.8F) << "the torus fell";
    EXPECT_NEAR(surface_volume(positions, boundary), rest, 0.05F * rest) << "the volume is kept";
    EXPECT_TRUE(take_validation_messages().empty());
}

auto
soft_body_test_name(const testing::TestParamInfo<std::tuple<std::size_t, GpuSolverMode>>& parameter) -> std::string
{
    return std::format("Device{}_{}", std::get<0>(parameter.param), std::get<1>(parameter.param));
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdSoftBodyTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         soft_body_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdSoftBodyTest);

// ===================================================================================================== measurements

/// The step time of a large cloth on each GPU (printed).
class GpuXpbdSoftBodyMeasurementTest : public GpuXpbdSoftBodyTest
{
};

TEST_P(GpuXpbdSoftBodyMeasurementTest, StepTimeIsMeasured)
{
    // A 64 × 64 cloth (4096 particles, 24 000 constraints) falling onto a sphere on a floor, colliding with itself: the
    // mean step time over 60 steps after 30, and the device memory the world holds then (its edges whip past the sphere
    // at 9 m/s: every box grows by that motion, yet only the pairs within their own reach take manifolds).
    Scene world = scene();
    world.floor();
    world.add_static(make_shape(SphereShape{.radius = 0.5F}), {.position = {.y = 0.5F}});
    SoftBodyDescription description = cloth_preset({.columns = 64, .rows = 64, .spacing = 0.03F, .origin = {.x = -0.945F, .y = 1.5F, .z = -0.945F}, .row_direction = {.z = 1}});
    description.self_collision = true;
    (void)world.soft(description);
    world.run(30);
    const auto start = std::chrono::steady_clock::now();
    world.run(60);
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60.0;
    const double mebibytes = static_cast<double>(m_device->memory_statistics().allocation_bytes) / 1048576.0;
    std::cout << std::format("{} | {} | a 64 x 64 cloth on a sphere: {:.2f} ms per step ({} contact points, {:.0f} MiB of device memory)\n",
                             m_physical_device->properties().name, mode(), milliseconds, world.world->statistics().contacts, mebibytes);
    EXPECT_LT(mebibytes, 1024.0);
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdSoftBodyMeasurementTest,
                         testing::Combine(testing::ValuesIn(physical_devices()), testing::Values(GpuSolverMode::GRAPH_COLORING, GpuSolverMode::JACOBI)),
                         soft_body_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdSoftBodyMeasurementTest);

} // namespace
