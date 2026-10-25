/// @file sequential_impulses.cppm
/// @brief The abstraction's rigid-body solver: sequential impulses (Catto) with warm starting, Coulomb friction with
///        static and dynamic coefficients (a point sticks while the impulse that stops it fits the static cone, and
///        slides at the dynamic coefficient beyond), rolling resistance, restitution above a threshold, speculative
///        contacts, every joint kind made of constraint rows (each run of equalities — a point, an orientation — solved
///        as one small system, as Box2D's point constraints are), user constraints, and position correction by
///        non-linear Gauss–Seidel iterations (no velocity bias: no energy is added to correct positions).
///
/// A step: the joints' and contacts' accumulated impulses of last step are applied again (warm starting); the velocity
/// iterations solve the joints' rows (equalities, limits, motors, soft springs) then the contacts' friction, rolling
/// resistance and normal impulses (a contact still apart allows the bodies to approach until they touch: speculative);
/// restitution is applied to the points that hit faster than the threshold; the bodies move by their velocities; the
/// position iterations then move them out of overlaps (leaving LINEAR_SLOP) and back onto their joints, by at most
/// MAXIMUM_LINEAR_CORRECTION and MAXIMUM_ANGULAR_CORRECTION per iteration. Joints and contacts are colored by the
/// bodies they move (no two of a color share a dynamic body): a color is solved in parallel on the task scheduler with
/// the same result as on one thread, so a step gives the same bits on any number of threads.
export module gfx.physics.dynamics:sequential_impulses;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.interface;

export namespace gfx::physics
{

/// @brief Sequential impulses, the reference solver (SolverKind::SEQUENTIAL_IMPULSE).
class SequentialImpulseSolver final : public Solver
{
public:
    SequentialImpulseSolver();
    ~SequentialImpulseSolver() override;

    SequentialImpulseSolver(const SequentialImpulseSolver&) = delete;
    auto operator=(const SequentialImpulseSolver&) -> SequentialImpulseSolver& = delete;

    [[nodiscard]] auto kind() const noexcept -> SolverKind override;
    [[nodiscard]] auto is_external() const noexcept -> bool override;

    /// @throws PhysicsError if the step's duration is not finite and positive, or a contact or joint names a body out of
    ///         range; what a user constraint throws.
    void solve(const SolverStep& step) override;

    /// @brief What it keeps between steps (its scratch storage; defined in its implementation unit).
    struct State;

private:
    std::unique_ptr<State> m_state;
};

/// @brief The rows of @p joint (a joint, not a user constraint) with its bodies at @p first and @p second (the world:
///        a default frame): a point held is three EQUALITY rows along the world axes, an axis kept is EQUALITY rows about
///        the frame's other axes, a limit two INEQUALITY rows, a motor a MOTOR row, a spring of a frequency a SPRING row
///        (a fixed joint's springs of frequency 0 hold rigidly; a distance spring of frequency 0 leaves the length free
///        between its limits). Hinges turn about their frames' z axis, sliders move along x, cone-twist joints twist
///        about z. The same description always gives the same rows in the same order (the solver keeps each row's
///        impulse from step to step).
/// @return How many rows were written into @p rows (at most MAXIMUM_CONSTRAINT_ROWS).
auto joint_rows(const SolverJoint& joint,
                const ConstraintFrame& first,
                const ConstraintFrame& second,
                std::span<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> rows) noexcept -> std::uint32_t;

} // namespace gfx::physics
