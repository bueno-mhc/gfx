module gfx.physics.dynamics;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.interface;

namespace gfx::physics
{

namespace
{

/// Throws the PhysicsError of a state whose spans differ in length, or of a step that is not finite and positive.
void
check_state(const IntegrationState& state,
            float seconds,
            bool needs_previous_positions,
            std::string_view operation)
{
    const std::size_t count = state.positions.size();
    const auto reject = [&](std::string problem, std::string suggestion) {
        throw PhysicsError({.backend = PhysicsSubsystem::DYNAMICS, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::move(suggestion)});
    };
    if (state.velocities.size() != count || state.inverse_masses.size() != count)
    {
        reject(std::format("the state has {} positions, {} velocities and {} inverse masses", count, state.velocities.size(), state.inverse_masses.size()),
               "give every span of the state the same length");
    }
    if (!state.forces.empty() && state.forces.size() != count)
    {
        reject(std::format("the state has {} forces for {} positions", state.forces.size(), count), "give one force per position, or none");
    }
    if (needs_previous_positions ? state.previous_positions.size() != count : !state.previous_positions.empty() && state.previous_positions.size() != count)
    {
        reject(std::format("the state has {} previous positions for {} positions", state.previous_positions.size(), count),
               needs_previous_positions ? "give the previous positions (start them at the positions minus the velocities times the step)"
                                        : "give one previous position per position, or none");
    }
    if (!(seconds > 0.0F) || !std::isfinite(seconds))
    {
        reject(std::format("the step of {} seconds is not finite and positive", seconds), "step by a positive time");
    }
}

/// The force field's forces at @p positions and @p velocities into @p forces (sized and zeroed first); empty without a
/// field.
auto
field_forces(const IntegrationState& state,
             std::span<const Vector3> positions,
             std::span<const Vector3> velocities,
             std::vector<Vector3>& forces) -> std::span<const Vector3>
{
    if (state.force_field == nullptr || !*state.force_field)
    {
        return {};
    }
    forces.assign(positions.size(), Vector3{});
    (*state.force_field)(positions, velocities, forces);
    return forces;
}

/// The acceleration of state @p index: gravity, then its inverse mass times its forces.
[[nodiscard]] auto
acceleration(const IntegrationState& state,
             std::span<const Vector3> field,
             std::size_t index) noexcept -> Vector3
{
    Vector3 force{};
    if (!state.forces.empty())
    {
        force += state.forces[index];
    }
    if (!field.empty())
    {
        force += field[index];
    }
    return state.gravity + force * state.inverse_masses[index];
}

/// Whether every state moves (no inverse mass of 0): the positions then advance in one SIMD batch.
[[nodiscard]] auto
all_movable(std::span<const float> inverse_masses) noexcept -> bool
{
    return std::ranges::all_of(inverse_masses, [](float inverse_mass) { return inverse_mass > 0.0F; });
}

/// Keeps the positions the step starts from, when asked.
void
keep_start(const IntegrationState& state)
{
    if (!state.previous_positions.empty())
    {
        std::ranges::copy(state.positions, state.previous_positions.begin());
    }
}

/// v += a h, then x += v h (the states that move).
void
semi_implicit_euler(const IntegrationState& state,
                    std::span<const Vector3> field,
                    float seconds)
{
    for (std::size_t index = 0; index < state.positions.size(); ++index)
    {
        if (state.inverse_masses[index] > 0.0F)
        {
            state.velocities[index] += acceleration(state, field, index) * seconds;
        }
    }
    if (all_movable(state.inverse_masses))
    {
        multiply_add(state.positions, state.velocities, seconds);
        return;
    }
    for (std::size_t index = 0; index < state.positions.size(); ++index)
    {
        if (state.inverse_masses[index] > 0.0F)
        {
            state.positions[index] += state.velocities[index] * seconds;
        }
    }
}

/// v = (x − x₋₁) / h for the states that move.
void
velocities_from_positions(const IntegrationState& state,
                          float seconds)
{
    const float rate = 1.0F / seconds;
    for (std::size_t index = 0; index < state.positions.size(); ++index)
    {
        if (state.inverse_masses[index] > 0.0F)
        {
            state.velocities[index] = (state.positions[index] - state.previous_positions[index]) * rate;
        }
    }
}

} // namespace

// ===================================================================================================== semi-implicit Euler

SemiImplicitEulerIntegrator::SemiImplicitEulerIntegrator() noexcept
    : Integrator{PhysicsSubsystem::DYNAMICS}
{
}

auto
SemiImplicitEulerIntegrator::kind() const noexcept -> IntegratorKind
{
    return IntegratorKind::SEMI_IMPLICIT_EULER;
}

auto
SemiImplicitEulerIntegrator::is_external() const noexcept -> bool
{
    return true;
}

auto
SemiImplicitEulerIntegrator::requirements() const noexcept -> IntegratorRequirements
{
    return {.previous_positions = false, .force_evaluations = 1};
}

void
SemiImplicitEulerIntegrator::predict(const IntegrationState& state,
                                     float seconds)
{
    // The forces at the start, then one step.
    check_state(state, seconds, false, "SemiImplicitEulerIntegrator::predict");
    keep_start(state);
    const std::span<const Vector3> field = field_forces(state, state.positions, state.velocities, m_field_forces);
    semi_implicit_euler(state, field, seconds);
}

void
SemiImplicitEulerIntegrator::finalize(const IntegrationState& /*state*/,
                                      float /*seconds*/)
{
}

// ===================================================================================================== Verlet

VerletIntegrator::VerletIntegrator() noexcept
    : Integrator{PhysicsSubsystem::DYNAMICS}
{
}

auto
VerletIntegrator::kind() const noexcept -> IntegratorKind
{
    return IntegratorKind::VERLET;
}

auto
VerletIntegrator::is_external() const noexcept -> bool
{
    return true;
}

auto
VerletIntegrator::requirements() const noexcept -> IntegratorRequirements
{
    return {.previous_positions = true, .force_evaluations = 1};
}

void
VerletIntegrator::predict(const IntegrationState& state,
                          float seconds)
{
    // The forces at the current positions; x' = 2x − x₋₁ + a h², the current positions become the previous ones.
    check_state(state, seconds, true, "VerletIntegrator::predict");
    const std::span<const Vector3> field = field_forces(state, state.positions, state.velocities, m_field_forces);
    const float squared = seconds * seconds;
    const float rate = 1.0F / seconds;
    for (std::size_t index = 0; index < state.positions.size(); ++index)
    {
        const Vector3 current = state.positions[index];
        if (state.inverse_masses[index] > 0.0F)
        {
            const Vector3 next = current * 2.0F - state.previous_positions[index] + acceleration(state, field, index) * squared;
            state.velocities[index] = (next - current) * rate;
            state.positions[index] = next;
        }
        state.previous_positions[index] = current;
    }
}

void
VerletIntegrator::finalize(const IntegrationState& state,
                           float seconds)
{
    check_state(state, seconds, true, "VerletIntegrator::finalize");
    velocities_from_positions(state, seconds);
}

// ===================================================================================================== Runge–Kutta 4

RungeKutta4Integrator::RungeKutta4Integrator() noexcept
    : Integrator{PhysicsSubsystem::DYNAMICS}
{
}

auto
RungeKutta4Integrator::kind() const noexcept -> IntegratorKind
{
    return IntegratorKind::RUNGE_KUTTA_4;
}

auto
RungeKutta4Integrator::is_external() const noexcept -> bool
{
    return true;
}

auto
RungeKutta4Integrator::requirements() const noexcept -> IntegratorRequirements
{
    return {.previous_positions = false, .force_evaluations = 4};
}

void
RungeKutta4Integrator::predict(const IntegrationState& state,
                               float seconds)
{
    check_state(state, seconds, false, "RungeKutta4Integrator::predict");
    keep_start(state);
    const std::size_t count = state.positions.size();
    m_stage_positions.assign(state.positions.begin(), state.positions.end());
    m_stage_velocities.assign(state.velocities.begin(), state.velocities.end());
    m_position_sums.assign(count, Vector3{});
    m_velocity_sums.assign(count, Vector3{});

    // Four stages: k = (v, a) at the stage's state, summed with weights 1, 2, 2, 1; the next stage's state is the start
    // plus the stage's derivative times h/2, h/2, then h.
    constexpr std::array<float, 4> WEIGHTS{1.0F, 2.0F, 2.0F, 1.0F};
    constexpr std::array<float, 4> NEXT_FRACTIONS{0.5F, 0.5F, 1.0F, 0.0F};
    for (std::size_t stage = 0; stage < 4; ++stage)
    {
        const std::span<const Vector3> field = field_forces(state, m_stage_positions, m_stage_velocities, m_field_forces);
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!(state.inverse_masses[index] > 0.0F))
            {
                continue;
            }
            const Vector3 position_rate = m_stage_velocities[index];
            const Vector3 velocity_rate = acceleration(state, field, index);
            m_position_sums[index] += position_rate * WEIGHTS[stage];
            m_velocity_sums[index] += velocity_rate * WEIGHTS[stage];
            m_stage_positions[index] = state.positions[index] + position_rate * (seconds * NEXT_FRACTIONS[stage]);
            m_stage_velocities[index] = state.velocities[index] + velocity_rate * (seconds * NEXT_FRACTIONS[stage]);
        }
    }

    // The weighted mean of the four derivatives.
    const float sixth = seconds / 6.0F;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (state.inverse_masses[index] > 0.0F)
        {
            state.positions[index] += m_position_sums[index] * sixth;
            state.velocities[index] += m_velocity_sums[index] * sixth;
        }
    }
}

void
RungeKutta4Integrator::finalize(const IntegrationState& /*state*/,
                                float /*seconds*/)
{
}

// ===================================================================================================== XPBD

XpbdIntegrator::XpbdIntegrator() noexcept
    : Integrator{PhysicsSubsystem::DYNAMICS}
{
}

auto
XpbdIntegrator::kind() const noexcept -> IntegratorKind
{
    return IntegratorKind::XPBD;
}

auto
XpbdIntegrator::is_external() const noexcept -> bool
{
    return true;
}

auto
XpbdIntegrator::requirements() const noexcept -> IntegratorRequirements
{
    return {.previous_positions = true, .force_evaluations = 1};
}

void
XpbdIntegrator::predict(const IntegrationState& state,
                        float seconds)
{
    // The positions kept, then predicted from the new velocities.
    check_state(state, seconds, true, "XpbdIntegrator::predict");
    keep_start(state);
    const std::span<const Vector3> field = field_forces(state, state.positions, state.velocities, m_field_forces);
    semi_implicit_euler(state, field, seconds);
}

void
XpbdIntegrator::finalize(const IntegrationState& state,
                         float seconds)
{
    check_state(state, seconds, true, "XpbdIntegrator::finalize");
    velocities_from_positions(state, seconds);
}

// ===================================================================================================== factory

auto
make_integrator(IntegratorKind kind) -> std::unique_ptr<Integrator>
{
    switch (kind)
    {
        case IntegratorKind::SEMI_IMPLICIT_EULER:
            return std::make_unique<SemiImplicitEulerIntegrator>();
        case IntegratorKind::VERLET:
            return std::make_unique<VerletIntegrator>();
        case IntegratorKind::RUNGE_KUTTA_4:
            return std::make_unique<RungeKutta4Integrator>();
        case IntegratorKind::XPBD:
            return std::make_unique<XpbdIntegrator>();
    }
    throw PhysicsError({.backend = PhysicsSubsystem::DYNAMICS, .operation = "make_integrator", .problem = std::format("{} is not an integrator kind", std::to_underlying(kind)),
                        .suggestion = "name an IntegratorKind"});
}

} // namespace gfx::physics
