// The abstraction's soft bodies on their own (no world): presets, models and their colors, and XPBD steps with known
// outcomes — a rope keeps its length, a cloth hanging from two pins settles, a bent hinge returns to its rest angle, a
// volume rests on a floor with its volume, particles rest on every kind of shape, friction holds on a slope steeper
// than its angle only, self-collision keeps particles apart, momentum is exchanged with a dynamic body, every integrator
// predicts, the same bits on one and four threads.
#include <gtest/gtest.h>

import std;
import gfx.physics;
import gfx.tests.physics_support;

namespace
{

using namespace gfx;
using namespace gfx::physics;
using gfx::tests::PhysicsHandles;

constexpr float STEP = 1.0F / 60.0F;
constexpr Vector3 GRAVITY{.y = -STANDARD_GRAVITY};

/// Rigid bodies and their colliders as soft bodies see them (they do not move unless a test moves them).
struct Scene
{
    std::vector<CoupledBody> bodies{};
    std::vector<CoupledCollider> colliders{};
    std::vector<std::shared_ptr<const Shape>> shapes{};

    /// A static body at @p pose with one collider of @p shape.
    auto add_static(std::shared_ptr<const Shape> shape,
                    const Transform& pose,
                    Friction friction = {}) -> std::uint32_t
    {
        const auto index = static_cast<std::uint32_t>(bodies.size());
        bodies.push_back({.handle = PhysicsHandles::make<RigidBodyTag>(index), .kind = BodyKind::STATIC, .start_pose = pose, .end_pose = pose, .mass = {}, .locks = {}});
        colliders.push_back({.handle = PhysicsHandles::make<ColliderTag>(index), .body = index, .shape = shape.get(), .pose = {}, .friction = friction, .filter = {}});
        shapes.push_back(std::move(shape));
        return index;
    }

    /// The floor: a box whose top is at y = 0.
    auto add_floor(Friction friction = {}) -> std::uint32_t
    {
        return add_static(make_shape(BoxShape{.half_extents = {.x = 20, .y = 0.5F, .z = 20}}), {.position = {.y = -0.5F}}, friction);
    }
};

/// One soft body and its particles.
struct Simulation
{
    SoftBodyDescription description;
    SoftBodyModel model;
    std::vector<Vector3> positions{};
    std::vector<Vector3> velocities{};

    explicit Simulation(SoftBodyDescription made)
        : description{std::move(made)}
        , model{description}
    {
        for (const SoftBodyParticle& particle : description.particles)
        {
            positions.push_back(particle.position);
            velocities.push_back(particle.velocity);
        }
    }

    [[nodiscard]] auto input() -> SimulatedSoftBody
    {
        return {.model = &model, .settings = &description, .positions = positions, .velocities = velocities};
    }

    /// The largest speed of a particle.
    [[nodiscard]] auto fastest() const -> float
    {
        return std::ranges::max(velocities | std::views::transform([](Vector3 velocity) { return length(velocity); }));
    }

    /// The largest relative stretch of its STRETCH constraints.
    [[nodiscard]] auto largest_stretch() const -> float
    {
        float largest = 0.0F;
        for (const SoftBodyModel::Constraint& constraint : model.constraints())
        {
            if (constraint.kind == SoftBodyConstraintKind::STRETCH)
            {
                largest = std::max(largest, distance(positions[constraint.particles[0]], positions[constraint.particles[1]]) / constraint.rest - 1.0F);
            }
        }
        return largest;
    }
};

/// @p steps steps of @p simulation in @p scene.
void
simulate(Simulation& simulation,
         const Scene& scene,
         std::uint32_t steps,
         Vector3 gravity = GRAVITY,
         Integrator* integrator = nullptr,
         TaskScheduler* scheduler = nullptr)
{
    XpbdSoftBodySolver solver;
    XpbdIntegrator default_integrator;
    const SimulatedSoftBody input = simulation.input();
    for (std::uint32_t step = 0; step < steps; ++step)
    {
        solver.step(STEP, gravity, std::span{&input, 1}, scene.bodies, scene.colliders, integrator != nullptr ? *integrator : default_integrator, scheduler);
    }
}

/// A rope of @p segments from the origin along +x, its first particle pinned where it starts.
[[nodiscard]] auto
pinned_rope(std::uint32_t segments,
            bool tethers) -> SoftBodyDescription
{
    SoftBodyDescription rope = rope_preset({.segments = segments, .length = 2.0F, .direction = {.x = 1}, .mass = 0.2F});
    rope.pins = {pin_to_world(rope, 0)};
    rope.damping = 0.8F;
    if (tethers)
    {
        add_long_range_attachments(rope);
    }
    return rope;
}

// ===================================================================================================== presets and models

TEST(SoftBodyPresetTest, ClothsRopesAndVolumesAreMadeAsDescribed)
{
    // A cloth of 4 × 3: 12 particles; 17 grid edges, 12 cell diagonals, 2 × 3 + 4 × 1 bending distances; its mass.
    const SoftBodyDescription cloth = cloth_preset({.columns = 4, .rows = 3, .spacing = 0.1F, .origin = {.y = 2}, .area_density = 0.5F});
    ASSERT_EQ(cloth.particles.size(), 12U);
    const auto count = [](const SoftBodyDescription& description, SoftBodyConstraintKind kind) {
        return std::ranges::count(description.constraints, kind, &SoftBodyConstraint::kind);
    };
    EXPECT_EQ(count(cloth, SoftBodyConstraintKind::STRETCH), 17);
    EXPECT_EQ(count(cloth, SoftBodyConstraintKind::SHEAR), 12);
    EXPECT_EQ(count(cloth, SoftBodyConstraintKind::BENDING), 10);
    float mass = 0.0F;
    for (const SoftBodyParticle& particle : cloth.particles)
    {
        mass += 1.0F / particle.inverse_mass;
    }
    EXPECT_NEAR(mass, 0.5F * 0.3F * 0.2F, 1.0e-6F);
    EXPECT_EQ(cloth.particles[cloth_particle(4, 3, 2)].position, (Vector3{.x = 0.3F, .y = 2.0F - 0.2F}));
    EXPECT_FLOAT_EQ(cloth.particle_radius, 0.05F);

    // With dihedral bending: one per cell diagonal and per inner grid edge.
    const SoftBodyDescription folded = cloth_preset({.columns = 4, .rows = 3, .dihedral_bending = true});
    EXPECT_EQ(count(folded, SoftBodyConstraintKind::BENDING), 0);
    EXPECT_EQ(count(folded, SoftBodyConstraintKind::DIHEDRAL), 6 + 3 + 4);

    // A rope: segments + 1 particles sharing its mass.
    const SoftBodyDescription rope = rope_preset({.segments = 5, .length = 1.0F, .mass = 0.6F});
    ASSERT_EQ(rope.particles.size(), 6U);
    EXPECT_FLOAT_EQ(rope.particles[0].inverse_mass, 10.0F);
    EXPECT_EQ(count(rope, SoftBodyConstraintKind::STRETCH), 5);
    EXPECT_EQ(count(rope, SoftBodyConstraintKind::BENDING), 4);
    EXPECT_NEAR(rope.particles[5].position.y, -1.0F, 1.0e-6F);

    // A tetrahedral box: (n + 1)³ vertices, 6n³ positive tetrahedra filling it; as a volume, its mass and unique edges.
    const TetrahedralMesh mesh = tetrahedral_box({.half_extents = {.x = 0.5F, .y = 0.25F, .z = 1.0F}, .cells = {2, 2, 2}, .pose = {.position = {.y = 3}}});
    ASSERT_EQ(mesh.positions.size(), 27U);
    ASSERT_EQ(mesh.tetrahedra.size(), 48U);
    float volume = 0.0F;
    for (const auto& [a, b, c, d] : mesh.tetrahedra)
    {
        const float six = dot(cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]), mesh.positions[d] - mesh.positions[a]);
        EXPECT_GT(six, 0.0F);
        volume += six / 6.0F;
    }
    EXPECT_NEAR(volume, 1.0F * 0.5F * 2.0F, 1.0e-5F);
    const SoftBodyDescription body = volume_preset(mesh, {.density = 10.0F});
    float body_mass = 0.0F;
    for (const SoftBodyParticle& particle : body.particles)
    {
        body_mass += 1.0F / particle.inverse_mass;
    }
    EXPECT_NEAR(body_mass, 10.0F, 1.0e-4F);
    EXPECT_EQ(count(body, SoftBodyConstraintKind::VOLUME), 48);
    EXPECT_EQ(count(body, SoftBodyConstraintKind::STRETCH), 54 + 36 + 8) << "every edge once: the grid's, a diagonal per square, one per cell";

    // Long-range attachments: one per free particle and anchor.
    SoftBodyDescription pinned = rope_preset({.segments = 4});
    pinned.pins = {pin_to_world(pinned, 0)};
    EXPECT_EQ(add_long_range_attachments(pinned), 4U);
    EXPECT_EQ(count(pinned, SoftBodyConstraintKind::TETHER), 4);

    // Invalid presets.
    EXPECT_THROW((void)cloth_preset({.columns = 1}), PhysicsError);
    EXPECT_THROW((void)cloth_preset({.column_direction = {.x = 1}, .row_direction = {.x = -2}}), PhysicsError);
    EXPECT_THROW((void)rope_preset({.segments = 0}), PhysicsError);
    EXPECT_THROW((void)tetrahedral_box({.cells = {0, 1, 1}}), PhysicsError);
    EXPECT_THROW((void)volume_preset({.positions = {Vector3{}, Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.x = 1, .y = 1}}, .tetrahedra = {{0, 1, 2, 3}}}), PhysicsError);
    EXPECT_THROW((void)pin_to_world(rope, 6), PhysicsError);
}

TEST(SoftBodyModelTest, ColorsNeverShareAParticleAndRestValuesComeFromTheStart)
{
    SoftBodyDescription cloth = cloth_preset({.columns = 12, .rows = 9, .dihedral_bending = true});
    cloth.pins = {pin_to_world(cloth, 0)};
    add_long_range_attachments(cloth);
    const SoftBodyModel model{cloth};

    // Every constraint but the tethers, once; tethers apart.
    const auto tethers = static_cast<std::size_t>(std::ranges::count(cloth.constraints, SoftBodyConstraintKind::TETHER, &SoftBodyConstraint::kind));
    EXPECT_EQ(model.constraints().size() + model.tethers().size(), cloth.constraints.size());
    EXPECT_EQ(model.tethers().size(), tethers);
    const std::span<const std::uint32_t> starts = model.color_starts();
    ASSERT_GE(starts.size(), 2U);
    EXPECT_EQ(starts.back(), model.constraints().size());

    // No particle twice in a color; rest values of the flat cloth's starting positions.
    for (std::size_t color = 0; color + 1 < starts.size(); ++color)
    {
        std::set<std::uint32_t> seen;
        for (std::uint32_t index = starts[color]; index < starts[color + 1]; ++index)
        {
            const SoftBodyModel::Constraint& constraint = model.constraints()[index];
            for (std::uint32_t corner = 0; corner < soft_body_constraint_particles(constraint.kind); ++corner)
            {
                EXPECT_TRUE(seen.insert(constraint.particles[corner]).second) << "color " << color;
            }
            if (constraint.kind == SoftBodyConstraintKind::DIHEDRAL)
            {
                EXPECT_NEAR(constraint.rest, 0.0F, 1.0e-6F);
            }
            else
            {
                EXPECT_NEAR(constraint.rest, distance(cloth.particles[constraint.particles[0]].position, cloth.particles[constraint.particles[1]].position), 1.0e-6F);
            }
        }
    }

    // Given rest values; constraints naming a particle twice or out of range.
    const SoftBodyModel given{{.particles = {{}, {.position = {.x = 1}}}, .constraints = {{.particles = {0, 1}, .rest_value = 0.5F}}}};
    EXPECT_FLOAT_EQ(given.constraints()[0].rest, 0.5F);
    EXPECT_THROW((SoftBodyModel{{.particles = {{}, {}}, .constraints = {{.particles = {1, 1}}}}}), PhysicsError);
    EXPECT_THROW((SoftBodyModel{{.particles = {{}, {}}, .constraints = {{.kind = SoftBodyConstraintKind::VOLUME, .particles = {0, 1, 2, 3}}}}}), PhysicsError);
}

// ===================================================================================================== simulations

TEST(XpbdSolverTest, ARopeHangingFromAPinKeepsItsLength)
{
    // Released horizontally, it swings down and settles under its pin; its segments hardly stretch, and not at all
    // beyond the reach of its long-range attachments.
    for (const bool tethers : {false, true})
    {
        SCOPED_TRACE(tethers ? "with long-range attachments" : "without");
        Simulation rope{pinned_rope(20, tethers)};
        const Scene scene;
        float stretch = 0.0F;
        XpbdSoftBodySolver solver;
        XpbdIntegrator integrator;
        const SimulatedSoftBody input = rope.input();
        for (std::uint32_t step = 0; step < 600; ++step)
        {
            solver.step(STEP, GRAVITY, std::span{&input, 1}, scene.bodies, scene.colliders, integrator, nullptr);
            float length = 0.0F;
            for (std::size_t index = 0; index + 1 < rope.positions.size(); ++index)
            {
                length += distance(rope.positions[index], rope.positions[index + 1]);
            }
            stretch = std::max(stretch, length / 2.0F - 1.0F);
        }
        EXPECT_LT(stretch, tethers ? 0.01F : 0.03F);
        EXPECT_EQ(rope.positions[0], Vector3{}) << "the pin holds";
        EXPECT_NEAR(rope.positions.back().x, 0.0F, 0.1F) << "under its pin";
        EXPECT_NEAR(rope.positions.back().y, -2.0F, 0.06F);
    }
}

TEST(XpbdSolverTest, AClothHangingFromTwoPinsSettles)
{
    for (const bool dihedral : {false, true})
    {
        SCOPED_TRACE(dihedral ? "dihedral bending" : "distance bending");
        SoftBodyDescription description = cloth_preset({.columns = 16, .rows = 16, .spacing = 0.05F, .origin = {.y = 2}, .dihedral_bending = dihedral});
        description.pins = {pin_to_world(description, cloth_particle(16, 0, 0)), pin_to_world(description, cloth_particle(16, 15, 0))};
        description.damping = 0.5F;
        Simulation cloth{std::move(description)};
        simulate(cloth, {}, 480);
        EXPECT_LT(cloth.fastest(), 0.02F) << "settled";
        EXPECT_EQ(cloth.positions[0], (Vector3{.y = 2})) << "pinned";
        EXPECT_EQ(cloth.positions[15], (Vector3{.x = 0.75F, .y = 2}));
        EXPECT_LT(cloth.largest_stretch(), 0.15F);
        EXPECT_LT(cloth.positions[cloth_particle(16, 8, 15)].y, 2.0F - 0.7F) << "hanging";
        EXPECT_NEAR(cloth.positions[cloth_particle(16, 8, 15)].z, 0.0F, 1.0e-4F) << "in its plane";
    }
}

TEST(XpbdSolverTest, ABentHingeReturnsToItsRestAngle)
{
    // Two triangles on a fixed edge: one wing lifted out of the plane; the dihedral constraint brings it to the rest
    // angle (0: flat; then 1 radian).
    const auto angle_of = [](const std::vector<Vector3>& positions) {
        const Vector3 edge = positions[1] - positions[0];
        const Vector3 first = normalized(cross(positions[2] - positions[0], positions[2] - positions[1]));
        const Vector3 second = normalized(cross(positions[3] - positions[1], positions[3] - positions[0]));
        return std::atan2(dot(cross(first, second), normalized(edge)), dot(first, second));
    };
    for (const float rest : {0.0F, 1.0F})
    {
        SCOPED_TRACE(rest);
        SoftBodyDescription hinge{.particles = {{.position = {}, .inverse_mass = 0.0F},
                                                {.position = {.x = 1}, .inverse_mass = 0.0F},
                                                {.position = {.x = 0.5F, .y = 1}},
                                                {.position = {.x = 0.5F, .y = -0.6F, .z = 0.8F}}},
                                  .constraints = {{.kind = SoftBodyConstraintKind::DIHEDRAL, .particles = {0, 1, 2, 3}, .rest_value = rest},
                                                  {.particles = {0, 2}},
                                                  {.particles = {1, 2}},
                                                  {.particles = {0, 3}},
                                                  {.particles = {1, 3}}},
                                  .damping = 0.8F};
        Simulation simulation{std::move(hinge)};
        EXPECT_GT(std::abs(angle_of(simulation.positions) - rest), 0.5F);
        simulate(simulation, {}, 240, {});
        EXPECT_NEAR(angle_of(simulation.positions), rest, 0.01F);
    }
}

TEST(XpbdSolverTest, AVolumeRestsOnTheFloorAndKeepsItsVolume)
{
    const TetrahedralMesh mesh = tetrahedral_box({.half_extents = {.x = 0.2F, .y = 0.2F, .z = 0.2F}, .cells = {3, 3, 3}, .pose = {.position = {.y = 1}}});
    SoftBodyDescription description = volume_preset(mesh);
    description.damping = 0.2F;
    Simulation volume{std::move(description)};
    Scene scene;
    scene.add_floor();
    simulate(volume, scene, 240);
    const auto lowest = std::ranges::min(volume.positions | std::views::transform(&Vector3::y));
    EXPECT_NEAR(lowest, volume.description.particle_radius, 2.0e-3F) << "on the floor";
    float total = 0.0F;
    for (const auto& [a, b, c, d] : mesh.tetrahedra)
    {
        total += dot(cross(volume.positions[b] - volume.positions[a], volume.positions[c] - volume.positions[a]), volume.positions[d] - volume.positions[a]) / 6.0F;
    }
    EXPECT_NEAR(total, 0.4F * 0.4F * 0.4F, 0.02F * 0.064F);
    EXPECT_LT(volume.fastest(), 0.02F);
}

TEST(XpbdSolverTest, ParticlesRestOnEveryKindOfShape)
{
    // A particle dropped from 0.3 above each shape's top comes to rest on it, a radius above.
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
        Scene scene;
        scene.add_static(shape.shape, shape.pose);
        Simulation particle{{.particles = {{.position = {.y = shape.top + 0.3F}}}, .particle_radius = RADIUS}};
        simulate(particle, scene, 120);
        EXPECT_NEAR(particle.positions[0].y, shape.top + RADIUS, 1.0e-3F);
        EXPECT_NEAR(particle.positions[0].x, 0.0F, 1.0e-4F);
        EXPECT_LT(particle.fastest(), 0.01F);
    }

    // Under a one-sided mesh a particle is inside: it comes out above; a two-sided one keeps it below.
    for (const bool two_sided : {false, true})
    {
        Scene scene;
        scene.add_static(make_shape(TriangleMeshShape{{.vertices = corners, .triangles = triangles, .two_sided = two_sided}}), {});
        Simulation particle{{.particles = {{.position = {.y = -0.02F}}}, .particle_radius = RADIUS}};
        simulate(particle, scene, 1, {});
        EXPECT_NEAR(particle.positions[0].y, two_sided ? -RADIUS : RADIUS, 1.0e-4F) << (two_sided ? "two-sided" : "one-sided");
    }
}

TEST(XpbdSolverTest, FrictionHoldsParticlesOnASlopeOnlyAboveItsAngle)
{
    // A slope of 30° (tan 30° = 0.577): friction 0.8 holds a particle, 0.2 lets it slide.
    constexpr float ANGLE = std::numbers::pi_v<float> / 6.0F;
    const Quaternion tilt = Quaternion::from_axis_angle({.z = 1}, ANGLE);
    const Vector3 up = rotate(tilt, {.y = 1});
    for (const float friction : {0.8F, 0.2F})
    {
        SCOPED_TRACE(friction);
        Scene scene;
        scene.add_static(make_shape(BoxShape{.half_extents = {.x = 5, .y = 0.5F, .z = 5}}), {.position = up * -0.5F, .orientation = tilt},
                         {.static_coefficient = friction, .dynamic_coefficient = friction});
        Simulation particle{{.particles = {{.position = up * 0.05F}}, .particle_radius = 0.05F, .damping = 0.0F, .friction = friction}};
        simulate(particle, scene, 60);
        const float slid = length(particle.positions[0] - up * 0.05F);
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
}

TEST(XpbdSolverTest, SelfCollisionKeepsTheParticlesOfABodyApart)
{
    // Two particles of one body thrown at each other: they pass through each other unless self-collision is on.
    for (const bool self_collision : {false, true})
    {
        SCOPED_TRACE(self_collision);
        Simulation pair{{.particles = {{.position = {.x = -0.5F}, .velocity = {.x = 1}}, {.position = {.x = 0.5F}, .velocity = {.x = -1}}},
                         .particle_radius = 0.05F,
                         .damping = 0.0F,
                         .self_collision = self_collision}};
        XpbdSoftBodySolver solver;
        XpbdIntegrator integrator;
        const SimulatedSoftBody input = pair.input();
        float closest = 1.0F;
        for (std::uint32_t step = 0; step < 60; ++step)
        {
            solver.step(STEP, {}, std::span{&input, 1}, {}, {}, integrator, nullptr);
            closest = std::min(closest, pair.positions[1].x - pair.positions[0].x);
        }
        if (self_collision)
        {
            EXPECT_GT(closest, 0.1F - 1.0e-4F);
            EXPECT_LT(pair.positions[0].x, 0.0F) << "back on its side";
        }
        else
        {
            EXPECT_LT(closest, 0.0F) << "through each other";
        }
    }
}

TEST(XpbdSolverTest, MomentumIsExchangedWithADynamicBody)
{
    // A soft cube thrown at a box floating at rest (no gravity): the box goes off with the momentum the cube lost; the
    // test applies the solver's corrections to the box and moves it, as a backend would.
    const TetrahedralMesh mesh = tetrahedral_box({.half_extents = {.x = 0.15F, .y = 0.15F, .z = 0.15F}, .cells = {2, 2, 2}});
    SoftBodyDescription description = volume_preset(mesh, {.density = 200.0F});
    description.damping = 0.0F;
    description.friction = 0.0F;
    for (SoftBodyParticle& particle : description.particles)
    {
        particle.velocity = {.x = 2};
    }
    Simulation cube{std::move(description)};
    float cube_mass = 0.0F;
    for (const SoftBodyParticle& particle : cube.description.particles)
    {
        cube_mass += 1.0F / particle.inverse_mass;
    }

    // The box: 2 kg, its inertia a box's.
    const auto shape = make_shape(BoxShape{.half_extents = {.x = 0.25F, .y = 0.25F, .z = 0.25F}});
    const MassProperties mass = shape->mass_properties(2.0F / 0.125F);
    Scene scene;
    scene.bodies.push_back({.handle = PhysicsHandles::make<RigidBodyTag>(0), .kind = BodyKind::DYNAMIC, .start_pose = {.position = {.x = 0.8F}},
                            .end_pose = {.position = {.x = 0.8F}}, .mass = mass, .locks = {}});
    scene.colliders.push_back({.handle = PhysicsHandles::make<ColliderTag>(0), .body = 0, .shape = shape.get(), .pose = {}, .friction = {}, .filter = {}});

    XpbdSoftBodySolver solver;
    XpbdIntegrator integrator;
    const SimulatedSoftBody input = cube.input();
    Vector3 box_velocity{};
    for (std::uint32_t step = 0; step < 60; ++step)
    {
        CoupledBody& box = scene.bodies[0];
        box.start_pose = box.end_pose;
        box.end_pose.position += box_velocity * STEP;
        solver.step(STEP, {}, std::span{&input, 1}, scene.bodies, scene.colliders, integrator, nullptr);
        for (const BodyCorrection& correction : solver.corrections())
        {
            box.end_pose.position += correction.translation;
            box_velocity += correction.linear_impulse / mass.mass;
        }
    }
    Vector3 cube_momentum{};
    for (std::size_t index = 0; index < cube.velocities.size(); ++index)
    {
        cube_momentum += cube.velocities[index] / cube.description.particles[index].inverse_mass;
    }
    EXPECT_GT(box_velocity.x, 0.5F) << "pushed";
    EXPECT_LT(cube_momentum.x, cube_mass * 2.0F * 0.9F) << "slowed";
    EXPECT_NEAR(cube_momentum.x + mass.mass * box_velocity.x, cube_mass * 2.0F, 1.0e-3F * cube_mass * 2.0F) << "momentum kept";
    EXPECT_NEAR(cube_momentum.y + mass.mass * box_velocity.y, 0.0F, 1.0e-3F);
}

TEST(XpbdSolverTest, EveryIntegratorPredictsSoftBodies)
{
    // A rope with long-range attachments settles to the same place whatever predicts it.
    std::vector<Vector3> reference;
    for (const IntegratorKind kind : {IntegratorKind::XPBD, IntegratorKind::SEMI_IMPLICIT_EULER, IntegratorKind::VERLET, IntegratorKind::RUNGE_KUTTA_4})
    {
        SCOPED_TRACE(std::format("{}", kind));
        Simulation rope{pinned_rope(10, true)};
        const std::unique_ptr<Integrator> integrator = make_integrator(kind);
        simulate(rope, {}, 300, GRAVITY, integrator.get());
        if (reference.empty())
        {
            reference = rope.positions;
        }
        for (std::size_t index = 0; index < reference.size(); ++index)
        {
            EXPECT_LT(distance(rope.positions[index], reference[index]), 0.02F) << index;
        }
        EXPECT_LT(rope.largest_stretch(), 0.01F);
    }
}

TEST(XpbdSolverTest, ResultsAreTheSameOnOneAndFourThreads)
{
    // A cloth large enough for its colors to run on the scheduler's threads, falling on a box: the same bits.
    const auto run = [](TaskScheduler* scheduler) {
        SoftBodyDescription description = cloth_preset({.columns = 48, .rows = 48, .spacing = 0.02F, .origin = {.x = -0.47F, .y = 0.7F, .z = -0.47F}, .row_direction = {.z = 1}});
        description.self_collision = true;
        Simulation cloth{std::move(description)};
        Scene scene;
        scene.add_floor();
        scene.add_static(make_shape(BoxShape{.half_extents = {.x = 0.3F, .y = 0.3F, .z = 0.3F}}), {.position = {.y = 0.3F}});
        simulate(cloth, scene, 20, GRAVITY, nullptr, scheduler);
        return cloth.positions;
    };
    const std::vector<Vector3> alone = run(nullptr);
    InlineTaskScheduler inline_scheduler;
    ThreadTaskScheduler threads{4};
    EXPECT_TRUE(alone == run(&inline_scheduler));
    EXPECT_TRUE(alone == run(&threads));
    EXPECT_LT(std::ranges::min(alone | std::views::transform(&Vector3::y)), 0.65F) << "it fell onto the box";
}

TEST(XpbdSolverTest, InvalidStepsAreRefused)
{
    Simulation rope{rope_preset({.segments = 3})};
    XpbdSoftBodySolver solver;
    XpbdIntegrator integrator;
    SimulatedSoftBody input = rope.input();
    EXPECT_THROW(solver.step(0.0F, GRAVITY, std::span{&input, 1}, {}, {}, integrator, nullptr), PhysicsError);
    input.positions = input.positions.first(2);
    EXPECT_THROW(solver.step(STEP, GRAVITY, std::span{&input, 1}, {}, {}, integrator, nullptr), PhysicsError);
    const SimulatedSoftBody empty{};
    EXPECT_THROW(solver.step(STEP, GRAVITY, std::span{&empty, 1}, {}, {}, integrator, nullptr), PhysicsError);
}

} // namespace
