/// @file integrators.cppm
/// @brief The abstraction's integrators: semi-implicit Euler, position Verlet, classical Runge–Kutta 4 and XPBD's
///        prediction and velocity update. Each advances particles (or point masses) given as structures of arrays
///        (IntegrationState): what soft bodies predict their positions with, and what an application may use on its own
///        particles.
///
/// Every integrator moves only states with a non-zero inverse mass; their acceleration is the state's gravity plus the
/// inverse mass times the accumulated forces (IntegrationState::forces) and the force field's forces, evaluated as often
/// as requirements().force_evaluations says (once by every integrator but Runge–Kutta 4, four times; without a force
/// field the forces are constant over the step). When IntegrationState::previous_positions is given, predict() leaves in
/// it the positions the step started from (Verlet first reads it as the positions one step earlier). An integrator keeps
/// scratch storage for the force field's forces: one state at a time (not concurrently from several threads).
export module gfx.physics.dynamics:integrators;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.interface;

export namespace gfx::physics
{

/// @brief Velocities first, then positions from the new velocities (symplectic Euler): v += a h, x += v h.
class SemiImplicitEulerIntegrator final : public Integrator
{
public:
    SemiImplicitEulerIntegrator() noexcept;

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override;
    [[nodiscard]] auto is_external() const noexcept -> bool override;
    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override;

    /// @throws PhysicsError if the spans of @p state differ in length or @p seconds is not finite and positive.
    void predict(const IntegrationState& state,
                 float seconds) override;

    /// @brief Nothing (the step is complete after predict()).
    void finalize(const IntegrationState& state,
                  float seconds) override;

private:
    std::vector<Vector3> m_field_forces{};
};

/// @brief Position Verlet (Störmer): x' = 2x − x₋₁ + a h², from the current and the previous positions (which must be
///        given; start them at x − v h). The velocities become the last step's displacement over h.
class VerletIntegrator final : public Integrator
{
public:
    VerletIntegrator() noexcept;

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override;
    [[nodiscard]] auto is_external() const noexcept -> bool override;
    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override;

    /// @throws PhysicsError if the spans of @p state differ in length (previous positions included) or @p seconds is not
    ///         finite and positive.
    void predict(const IntegrationState& state,
                 float seconds) override;

    /// @brief The velocities from the positions and the previous positions (after constraints moved the positions).
    void finalize(const IntegrationState& state,
                  float seconds) override;

private:
    std::vector<Vector3> m_field_forces{};
};

/// @brief Classical fourth-order Runge–Kutta on positions and velocities, the forces evaluated four times per step
///        (at the start, twice at the middle, at the end) through the force field.
class RungeKutta4Integrator final : public Integrator
{
public:
    RungeKutta4Integrator() noexcept;

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override;
    [[nodiscard]] auto is_external() const noexcept -> bool override;
    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override;

    /// @throws PhysicsError if the spans of @p state differ in length or @p seconds is not finite and positive.
    void predict(const IntegrationState& state,
                 float seconds) override;

    /// @brief Nothing.
    void finalize(const IntegrationState& state,
                  float seconds) override;

private:
    std::vector<Vector3> m_stage_positions{};
    std::vector<Vector3> m_stage_velocities{};
    std::vector<Vector3> m_field_forces{};
    std::vector<Vector3> m_position_sums{};
    std::vector<Vector3> m_velocity_sums{};
};

/// @brief XPBD's integration around constraint projection: predict() keeps the positions and moves them by the new
///        velocities (as semi-implicit Euler), finalize() makes the velocities the positions' change over the step.
class XpbdIntegrator final : public Integrator
{
public:
    XpbdIntegrator() noexcept;

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override;
    [[nodiscard]] auto is_external() const noexcept -> bool override;
    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override;

    /// @throws PhysicsError if the spans of @p state differ in length (previous positions included) or @p seconds is not
    ///         finite and positive.
    void predict(const IntegrationState& state,
                 float seconds) override;

    /// @brief The velocities from the (projected) positions and those predict() kept.
    void finalize(const IntegrationState& state,
                  float seconds) override;

private:
    std::vector<Vector3> m_field_forces{};
};

/// @brief An integrator of @p kind.
[[nodiscard]] auto make_integrator(IntegratorKind kind) -> std::unique_ptr<Integrator>;

} // namespace gfx::physics
