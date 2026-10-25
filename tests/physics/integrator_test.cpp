// The integrators against analytic motion: a spring (energy bounded for the symplectic ones, Runge–Kutta 4 within a
// tight bound and on the exact trajectory), a circular Kepler orbit (radius and energy bounded, angular momentum kept),
// immovable states, the checks of a state's spans, the factory.
#include <gtest/gtest.h>

import std;
import gfx.physics;

namespace
{

using namespace gfx;
using namespace gfx::physics;

constexpr std::array<IntegratorKind, 4> KINDS{IntegratorKind::SEMI_IMPLICIT_EULER, IntegratorKind::VERLET, IntegratorKind::RUNGE_KUTTA_4, IntegratorKind::XPBD};

/// One particle of mass 1 under @p field, advanced @p steps times by @p seconds; @p observe sees its position and
/// velocity after each step.
template<class Observe>
void
run(Integrator& integrator,
    const ForceField& field,
    Vector3 position,
    Vector3 velocity,
    float seconds,
    std::uint32_t steps,
    Observe&& observe)
{
    std::array<Vector3, 1> positions{position};
    std::array<Vector3, 1> velocities{velocity};
    std::array<Vector3, 1> previous{position - velocity * seconds};
    const std::array<float, 1> inverse_masses{1.0F};
    const IntegrationState state{.positions = positions,
                                 .velocities = velocities,
                                 .inverse_masses = inverse_masses,
                                 .forces = {},
                                 .previous_positions = previous,
                                 .gravity = {},
                                 .force_field = &field};
    for (std::uint32_t step = 0; step < steps; ++step)
    {
        integrator.predict(state, seconds);
        integrator.finalize(state, seconds);
        observe(step, positions[0], velocities[0]);
    }
}

TEST(IntegratorTest, ASpringKeepsItsEnergyWithinEachSchemesBound)
{
    // x'' = −4x from x = 1 at rest: x = cos 2t, energy 2; ten periods.
    constexpr float STIFFNESS = 4.0F;
    constexpr float SECONDS = 0.01F;
    constexpr auto STEPS = static_cast<std::uint32_t>(10.0F * std::numbers::pi_v<float> / SECONDS);
    const ForceField spring = [](std::span<const Vector3> positions, std::span<const Vector3> /*velocities*/, std::span<Vector3> forces) {
        forces[0] = positions[0] * -STIFFNESS;
    };
    for (const IntegratorKind kind : KINDS)
    {
        SCOPED_TRACE(std::format("{}", kind));
        const std::unique_ptr<Integrator> integrator = make_integrator(kind);
        EXPECT_EQ(integrator->kind(), kind);
        EXPECT_TRUE(integrator->is_external());
        float worst = 0.0F;
        float first_period = 0.0F;
        float last_period = 0.0F;
        float trajectory = 0.0F;
        run(*integrator, spring, {.x = 1}, {}, SECONDS, STEPS, [&](std::uint32_t step, Vector3 position, Vector3 velocity) {
            const float energy = 0.5F * length_squared(velocity) + 0.5F * STIFFNESS * length_squared(position);
            const float error = std::abs(energy - 2.0F) / 2.0F;
            worst = std::max(worst, error);
            if (step < STEPS / 10)
            {
                first_period = std::max(first_period, error);
            }
            if (step >= STEPS - STEPS / 10)
            {
                last_period = std::max(last_period, error);
            }
            trajectory = std::max(trajectory, std::abs(position.x - std::cos(2.0F * static_cast<float>(step + 1) * SECONDS)));
        });
        if (kind == IntegratorKind::RUNGE_KUTTA_4)
        {
            EXPECT_LT(worst, 1.0e-4F);
            EXPECT_LT(trajectory, 1.0e-3F) << "on the exact trajectory";
        }
        else
        {
            // Symplectic: the error stays within O(hω), and does not grow from the first period to the last.
            EXPECT_LT(worst, 0.025F);
            EXPECT_LT(last_period, first_period * 1.1F + 1.0e-5F) << "no drift";
        }
    }
}

TEST(IntegratorTest, ACircularOrbitStaysCircularAndKeepsItsAngularMomentum)
{
    // A point around a unit mass at radius 1 and speed 1 (period 2π, energy −1/2, angular momentum 1); three orbits.
    constexpr float SECONDS = 0.005F;
    constexpr auto STEPS = static_cast<std::uint32_t>(6.0F * std::numbers::pi_v<float> / SECONDS);
    const ForceField gravity = [](std::span<const Vector3> positions, std::span<const Vector3> /*velocities*/, std::span<Vector3> forces) {
        const float radius = length(positions[0]);
        forces[0] = positions[0] * (-1.0F / (radius * radius * radius));
    };
    for (const IntegratorKind kind : KINDS)
    {
        SCOPED_TRACE(std::format("{}", kind));
        const std::unique_ptr<Integrator> integrator = make_integrator(kind);
        float radius_error = 0.0F;
        float energy_error = 0.0F;
        float momentum_error = 0.0F;
        run(*integrator, gravity, {.x = 1}, {.y = 1}, SECONDS, STEPS, [&](std::uint32_t /*step*/, Vector3 position, Vector3 velocity) {
            const float radius = length(position);
            radius_error = std::max(radius_error, std::abs(radius - 1.0F));
            energy_error = std::max(energy_error, std::abs(0.5F * length_squared(velocity) - 1.0F / radius + 0.5F));
            momentum_error = std::max(momentum_error, std::abs(cross(position, velocity).z - 1.0F));
        });
        if (kind == IntegratorKind::RUNGE_KUTTA_4)
        {
            EXPECT_LT(radius_error, 1.0e-4F);
            EXPECT_LT(energy_error, 1.0e-4F);
            EXPECT_LT(momentum_error, 1.0e-3F);
        }
        else
        {
            EXPECT_LT(radius_error, 0.02F);
            EXPECT_LT(energy_error, 0.02F);
            EXPECT_LT(momentum_error, 1.0e-3F) << "central forces keep the angular momentum of symplectic schemes (but rounding)";
        }
    }
}

TEST(IntegratorTest, ImmovableStatesStayAndInvalidStatesAreRefused)
{
    // Gravity and forces move the first state, not the second (inverse mass 0).
    for (const IntegratorKind kind : KINDS)
    {
        SCOPED_TRACE(std::format("{}", kind));
        const std::unique_ptr<Integrator> integrator = make_integrator(kind);
        std::array<Vector3, 2> positions{Vector3{}, Vector3{.x = 5}};
        std::array<Vector3, 2> velocities{};
        std::array<Vector3, 2> previous = positions;
        const std::array<float, 2> inverse_masses{0.5F, 0.0F};
        const std::array<Vector3, 2> forces{Vector3{.x = 2}, Vector3{.x = 2}};
        const IntegrationState state{.positions = positions, .velocities = velocities, .inverse_masses = inverse_masses, .forces = forces, .previous_positions = previous,
                                     .gravity = {.y = -10}, .force_field = nullptr};
        integrator->predict(state, 0.1F);
        integrator->finalize(state, 0.1F);
        EXPECT_NEAR(velocities[0].x, 0.1F, 1.0e-5F) << "a = g + w f";
        EXPECT_NEAR(velocities[0].y, -1.0F, 1.0e-5F);
        EXPECT_EQ(positions[1], (Vector3{.x = 5}));
        EXPECT_EQ(previous[1], (Vector3{.x = 5})) << "the previous positions are where the step started";

        // Spans of different lengths, a step that is not positive.
        std::array<Vector3, 1> short_velocities{};
        EXPECT_THROW(integrator->predict({.positions = positions, .velocities = short_velocities, .inverse_masses = inverse_masses}, 0.1F), PhysicsError);
        EXPECT_THROW(integrator->predict(state, 0.0F), PhysicsError);
        EXPECT_THROW(integrator->predict(state, std::numeric_limits<float>::infinity()), PhysicsError);
    }

    // Verlet and XPBD need the previous positions; the others take them when given.
    std::array<Vector3, 1> positions{};
    std::array<Vector3, 1> velocities{};
    const std::array<float, 1> inverse_masses{1.0F};
    const IntegrationState bare{.positions = positions, .velocities = velocities, .inverse_masses = inverse_masses};
    EXPECT_THROW(VerletIntegrator{}.predict(bare, 0.1F), PhysicsError);
    EXPECT_THROW(XpbdIntegrator{}.predict(bare, 0.1F), PhysicsError);
    EXPECT_NO_THROW(SemiImplicitEulerIntegrator{}.predict(bare, 0.1F));
    EXPECT_NO_THROW(RungeKutta4Integrator{}.predict(bare, 0.1F));
    EXPECT_EQ(VerletIntegrator{}.requirements(), (IntegratorRequirements{.previous_positions = true, .force_evaluations = 1}));
    EXPECT_EQ(RungeKutta4Integrator{}.requirements().force_evaluations, 4U);
    EXPECT_THROW((void)make_integrator(static_cast<IntegratorKind>(200)), PhysicsError);
}

TEST(IntegratorTest, RungeKuttaEvaluatesTheFieldFourTimesAndIsExactUnderConstantForces)
{
    // Constant acceleration: x = ½ a t² exactly; the field called four times per step.
    std::uint32_t calls = 0;
    const ForceField constant = [&calls](std::span<const Vector3> /*positions*/, std::span<const Vector3> /*velocities*/, std::span<Vector3> forces) {
        ++calls;
        forces[0] = {.x = 3};
    };
    RungeKutta4Integrator integrator;
    std::uint32_t steps = 0;
    Vector3 final_position{};
    run(integrator, constant, {}, {}, 0.1F, 10, [&](std::uint32_t /*step*/, Vector3 position, Vector3 /*velocity*/) {
        ++steps;
        final_position = position;
    });
    EXPECT_EQ(calls, 4 * steps);
    EXPECT_NEAR(final_position.x, 0.5F * 3.0F * 1.0F * 1.0F, 1.0e-5F);
}

} // namespace
