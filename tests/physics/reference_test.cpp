// The reference backend: the factory's worlds of the abstraction's components, its integrator's fall exactly as
// semi-implicit Euler over its sub-steps predicts, discrete bodies tunneling through a thin wall where continuous ones are
// stopped, user constraints in detail (forces, breaking, disabling, refusals), its components replaced one by one. The
// scenarios every backend shares are the conformance suite's (gfx_physics_conformance_tests).
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

constexpr float STEP = 1.0F / 60.0F;
constexpr float PI = std::numbers::pi_v<float>;

[[nodiscard]] auto
sphere(float radius) -> std::shared_ptr<const Shape>
{
    return make_shape(SphereShape{.radius = radius});
}

[[nodiscard]] auto
box(Vector3 half_extents) -> std::shared_ptr<const Shape>
{
    return make_shape(BoxShape{.half_extents = half_extents});
}

[[nodiscard]] auto
reference_world(const PhysicsWorldDescription& description = {}) -> std::unique_ptr<PhysicsWorld>
{
    return create_physics_world(description, PhysicsBackendKind::REFERENCE);
}

/// A static floor (a box whose top is at y = 0).
struct Floor
{
    std::unique_ptr<RigidBody> body;
    std::unique_ptr<Collider> collider;
};

[[nodiscard]] auto
make_floor(PhysicsWorld& world,
           const SurfaceMaterial& material = {}) -> Floor
{
    Floor floor{.body = world.create_rigid_body({.kind = BodyKind::STATIC}), .collider = nullptr};
    floor.collider = floor.body->add_collider({.shape = box({.x = 50, .y = 0.5F, .z = 50}), .pose = {.position = {.y = -0.5F}}, .material = material});
    return floor;
}

/// A dynamic body with one collider.
struct Body
{
    std::unique_ptr<RigidBody> body;
    std::unique_ptr<Collider> collider;
};

[[nodiscard]] auto
make_body(PhysicsWorld& world,
          const RigidBodyDescription& description,
          const ColliderDescription& collider) -> Body
{
    Body made{.body = world.create_rigid_body(description), .collider = nullptr};
    made.collider = made.body->add_collider(collider);
    return made;
}

void
run(PhysicsWorld& world,
    int steps)
{
    for (int step = 0; step < steps; ++step)
    {
        world.step(STEP);
    }
}

// ===================================================================================================== the Box3D scenarios

TEST(ReferenceTest, TheFactoryMakesReferenceWorldsOfTheAbstractionsComponents)
{
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    EXPECT_TRUE(world->backend() == PhysicsBackendKind::REFERENCE);
    EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::BVH);
    EXPECT_TRUE(world->broad_phase().is_external());
    EXPECT_EQ(world->narrow_phase().kind(), NarrowPhaseKind::GJK_EPA);
    EXPECT_EQ(world->solver().kind(), SolverKind::SEQUENTIAL_IMPULSE);
    EXPECT_TRUE(world->solver().is_external());
    EXPECT_EQ(world->integrator().kind(), IntegratorKind::SEMI_IMPLICIT_EULER);
    EXPECT_EQ(world->soft_body_integrator().kind(), IntegratorKind::XPBD);
}

TEST(ReferenceTest, ABodyFallsFreely)
{
    // Semi-implicit Euler over the four sub-steps: the velocity is exact, the drop a sub-step's worth more than ½ g t².
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body ball = make_body(*world, {.pose = {.position = {.y = 100}}, .angular_damping = 0.0F}, {.shape = sphere(0.5F)});
    run(*world, 60);
    const RigidBodyState state = ball.body->state();
    EXPECT_NEAR(state.linear_velocity.y, -STANDARD_GRAVITY, 1e-3F);
    EXPECT_NEAR(100.0F - state.pose.position.y, 0.5F * STANDARD_GRAVITY * (1.0F + STEP / 4.0F), 1e-3F);
    EXPECT_NEAR(ball.body->mass_properties().mass, 4.0F / 3.0F * PI * 0.125F * 1000.0F, 1.0F);
}

// ===================================================================================================== what only the reference simulates

TEST(ReferenceTest, ContinuousBodiesDoNotTunnelThroughThinWalls)
{
    // A small ball at 150 m/s toward a 5 cm wall: through it when discrete, stopped when continuous.
    for (const MotionQuality quality : {MotionQuality::DISCRETE, MotionQuality::CONTINUOUS})
    {
        const std::unique_ptr<PhysicsWorld> world = reference_world({.gravity = {}});
        const std::unique_ptr<RigidBody> wall = world->create_rigid_body({.kind = BodyKind::STATIC, .pose = {.position = {.x = 5.0F}}});
        const std::unique_ptr<Collider> wall_collider = wall->add_collider({.shape = box({.x = 0.025F, .y = 2.0F, .z = 2.0F})});
        const Body bullet = make_body(*world, {.pose = {.position = {.x = 0.3F}}, .linear_velocity = {.x = 150.0F}, .motion_quality = quality}, {.shape = sphere(0.05F)});
        run(*world, 10);
        if (quality == MotionQuality::CONTINUOUS)
        {
            EXPECT_LT(bullet.body->state().pose.position.x, 5.0F) << "stopped by the wall";
        }
        else
        {
            EXPECT_GT(bullet.body->state().pose.position.x, 5.0F) << "through the wall";
        }
    }
}

/// Keeps the first body's center at a height (one row).
class HeightConstraint final : public UserConstraint
{
public:
    explicit HeightConstraint(float height) noexcept
        : m_height{height}
    {
    }

    [[nodiscard]] auto row_count() const noexcept -> std::uint32_t override
    {
        return 1;
    }

    void rows(const ConstraintFrame& first,
              const ConstraintFrame& /*second*/,
              std::span<ConstraintRow> rows) const noexcept override
    {
        rows[0] = {.kind = ConstraintRowKind::EQUALITY, .linear_first = {.y = 1.0F}, .error = first.center.y - m_height};
    }

private:
    float m_height;
};

TEST(ReferenceTest, UserConstraintsHoldTheirBodiesAndBreak)
{
    // A ball held at 2 m by its constraint (sideways it moves freely); a heavy one breaks a weak constraint.
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body held = make_body(*world, {.pose = {.position = {.y = 2.0F}}, .linear_velocity = {.x = 1.0F}}, {.shape = sphere(0.1F)});
    const std::unique_ptr<Constraint> height = world->create_constraint({.first = held.body->handle(), .constraint = std::make_shared<HeightConstraint>(2.0F)});
    const Body heavy = make_body(*world, {.pose = {.position = {.x = 3.0F, .y = 2.0F}}}, {.shape = sphere(0.3F), .density = 5000.0F});
    const std::unique_ptr<Constraint> weak = world->create_constraint(
        {.first = heavy.body->handle(), .constraint = std::make_shared<HeightConstraint>(2.0F), .breaking = {.force = 100.0F}});
    EXPECT_EQ(height->bodies().first, held.body->handle());
    bool broke = false;
    for (int step = 0; step < 60; ++step)
    {
        world->step(STEP);
        broke = broke || std::ranges::any_of(world->events().joints, [&weak](const JointEvent& event) { return event.constraint == weak->handle(); });
    }
    EXPECT_NEAR(held.body->state().pose.position.y, 2.0F, 0.01F);
    EXPECT_NEAR(held.body->state().pose.position.x, 1.0F, 0.05F);
    EXPECT_NEAR(length(height->state().force), held.body->mass_properties().mass * STANDARD_GRAVITY, 0.05F * held.body->mass_properties().mass * STANDARD_GRAVITY)
        << "it carries the ball's weight";
    EXPECT_TRUE(broke);
    EXPECT_TRUE(weak->state().broken);
    EXPECT_LT(heavy.body->state().pose.position.y, 0.0F);

    // Disabled, the ball falls; destroyed with its body, the constraint's object reports it.
    height->set_enabled(false);
    run(*world, 30);
    EXPECT_LT(held.body->state().pose.position.y, 1.5F);
    EXPECT_THROW((void)world->create_constraint({.first = held.body->handle(), .constraint = nullptr}), PhysicsError);
    EXPECT_THROW((void)world->create_constraint({.first = held.body->handle(), .second = held.body->handle(), .constraint = std::make_shared<HeightConstraint>(1.0F)}),
                 PhysicsError);
}

// ===================================================================================================== components

/// A broad phase that counts its updates around a spatial broad phase on another structure.
template<class Structure>
class CountingBroadPhase final : public BroadPhase
{
public:
    CountingBroadPhase() noexcept
        : BroadPhase{PhysicsSubsystem::SPATIAL}
    {
    }

    std::uint32_t updates = 0;

    [[nodiscard]] auto kind() const noexcept -> BroadPhaseKind override
    {
        return m_inner.kind();
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    auto insert(ColliderHandle collider,
                const Aabb& box,
                const CollisionFilter& filter) -> SpatialProxyHandle override
    {
        return m_inner.insert(collider, box, filter);
    }

    void remove(SpatialProxyHandle proxy) override
    {
        m_inner.remove(proxy);
    }

    auto move(SpatialProxyHandle proxy,
              const Aabb& box,
              Vector3 displacement) -> bool override
    {
        return m_inner.move(proxy, box, displacement);
    }

    void set_filter(SpatialProxyHandle proxy,
                    const CollisionFilter& filter) override
    {
        m_inner.set_filter(proxy, filter);
    }

    void update(TaskScheduler* scheduler) override
    {
        ++updates;
        m_inner.update(scheduler);
    }

    [[nodiscard]] auto pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_inner.pairs();
    }

    [[nodiscard]] auto new_pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_inner.new_pairs();
    }

    [[nodiscard]] auto ended_pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_inner.ended_pairs();
    }

    void structure_nodes(std::vector<SpatialNode>& nodes) const override
    {
        m_inner.structure_nodes(nodes);
    }

private:
    SpatialBroadPhaseComponent<Structure> m_inner{};
};

/// A narrow phase that counts the pairs it collides.
class CountingNarrowPhase final : public NarrowPhase
{
public:
    CountingNarrowPhase() noexcept
        : NarrowPhase{PhysicsSubsystem::NARROW_PHASE}
    {
    }

    mutable std::atomic<std::uint32_t> pairs{0};

    [[nodiscard]] auto kind() const noexcept -> NarrowPhaseKind override
    {
        return NarrowPhaseKind::GJK_EPA;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void collide(const ShapeInstance& first,
                 const ShapeInstance& second,
                 ContactCollector& contacts,
                 const CollisionSettings& settings) const override
    {
        ++pairs;
        gfx::physics::collide(first, second, contacts, settings);
    }
};

/// A solver that counts its steps around sequential impulses.
class CountingSolver final : public Solver
{
public:
    CountingSolver() noexcept
        : Solver{PhysicsSubsystem::DYNAMICS}
    {
    }

    std::uint32_t steps = 0;
    std::size_t contacts = 0;

    [[nodiscard]] auto kind() const noexcept -> SolverKind override
    {
        return SolverKind::SEQUENTIAL_IMPULSE;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void solve(const SolverStep& step) override
    {
        ++steps;
        contacts = std::max(contacts, step.contacts.size());
        m_inner.solve(step);
    }

private:
    SequentialImpulseSolver m_inner{};
};

/// An integrator that doubles gravity (forces twice as strong).
class DoubleForceIntegrator final : public Integrator
{
public:
    DoubleForceIntegrator() noexcept
        : Integrator{PhysicsSubsystem::DYNAMICS}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override
    {
        return IntegratorKind::SEMI_IMPLICIT_EULER;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override
    {
        return {};
    }

    void predict(const IntegrationState& state,
                 float seconds) override
    {
        for (std::size_t index = 0; index < state.positions.size(); ++index)
        {
            state.velocities[index] += (state.gravity + state.forces[index] * state.inverse_masses[index]) * (2.0F * seconds);
        }
    }

    void finalize(const IntegrationState& /*state*/,
                  float /*seconds*/) override
    {
    }
};

TEST(ReferenceTest, EachComponentCanBeReplaced)
{
    // A stack on every broad phase, with the application's narrow phase and solver: the same rest, each component used.
    for (int structure = 0; structure < 3; ++structure)
    {
        SCOPED_TRACE(structure);
        const std::unique_ptr<PhysicsWorld> world = reference_world();
        const Floor floor = make_floor(*world);
        std::vector<Body> boxes;
        for (int level = 0; level < 3; ++level)
        {
            boxes.push_back(make_body(*world, {.pose = {.position = {.y = 0.5F + static_cast<float>(level) * 1.01F}}}, {.shape = box({.x = 0.5F, .y = 0.5F, .z = 0.5F})}));
        }
        world->step(STEP);
        std::uint32_t updates = 0;
        if (structure == 0)
        {
            auto broad_phase = std::make_unique<CountingBroadPhase<SweepAndPrune<3>>>();
            auto* counting = broad_phase.get();
            world->replace_broad_phase(std::move(broad_phase));
            EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::SWEEP_AND_PRUNE);
            run(*world, 120);
            updates = counting->updates;
        }
        else if (structure == 1)
        {
            auto broad_phase = std::make_unique<CountingBroadPhase<Octree<>>>();
            auto* counting = broad_phase.get();
            world->replace_broad_phase(std::move(broad_phase));
            EXPECT_EQ(world->broad_phase().kind(), BroadPhaseKind::OCTREE);
            run(*world, 120);
            updates = counting->updates;

            // The octree shows its cells: the floor's (a coarse level) and the boxes', each listed once.
            std::vector<SpatialNode> nodes;
            world->broad_phase().structure_nodes(nodes);
            EXPECT_EQ(std::ranges::fold_left(nodes, 0U, [](std::uint32_t sum, const SpatialNode& node) { return sum + node.proxies; }), 4U);
            EXPECT_TRUE(std::ranges::any_of(nodes, [](const SpatialNode& node) { return node.depth > 0; }));
        }
        else
        {
            auto narrow_phase = std::make_unique<CountingNarrowPhase>();
            auto solver = std::make_unique<CountingSolver>();
            auto* narrow = narrow_phase.get();
            auto* counting_solver = solver.get();
            world->replace_narrow_phase(std::move(narrow_phase));
            world->replace_solver(std::move(solver));
            run(*world, 120);
            EXPECT_GT(narrow->pairs.load(), 0U);
            EXPECT_EQ(counting_solver->steps, 4U * 120U) << "once per sub-step";
            EXPECT_GE(counting_solver->contacts, 3U);
            updates = 120;
        }
        EXPECT_EQ(updates, 120U);
        for (std::size_t level = 0; level < boxes.size(); ++level)
        {
            EXPECT_NEAR(boxes[level].body->state().pose.position.y, 0.5F + static_cast<float>(level), 0.03F) << "level " << level;
        }
    }

    // The integrator: twice the forces, twice the fall's speed; null components are refused.
    const std::unique_ptr<PhysicsWorld> world = reference_world();
    const Body ball = make_body(*world, {.pose = {.position = {.y = 100}}}, {.shape = sphere(0.5F)});
    world->replace_integrator(std::make_unique<DoubleForceIntegrator>());
    run(*world, 30);
    EXPECT_NEAR(ball.body->state().linear_velocity.y, -STANDARD_GRAVITY, 1e-2F) << "half a second at 2 g";
    world->replace_integrator(make_integrator(IntegratorKind::RUNGE_KUTTA_4));
    run(*world, 30);
    EXPECT_NEAR(ball.body->state().linear_velocity.y, -1.5F * STANDARD_GRAVITY, 1e-2F) << "then half a second at g";
    EXPECT_THROW(world->replace_broad_phase(nullptr), PhysicsError);
    EXPECT_THROW(world->replace_solver(nullptr), PhysicsError);
}

} // namespace
