/// @file soft_body_solver.cppm
/// @brief The abstraction's soft bodies: XPBD (extended position-based dynamics) on particles, coupled both ways with the
///        rigid bodies of any backend.
///
/// A step of a soft body is divided into its sub-steps (SoftBodyDescription::sub_steps); each predicts the particles'
/// positions with the integrator, projects every constraint once — distances (STRETCH, SHEAR, BENDING), DIHEDRAL angles,
/// tetrahedral VOLUMEs, TETHERs, pins, contacts with the colliders, contacts between the body's own particles —, then
/// makes the velocities the positions' change over the sub-step and damps them (small steps: one iteration per
/// sub-step, Macklin et al. 2019). Compliance makes a constraint's stiffness independent of the step and of the
/// sub-step count. The constraints a body is made of are grouped in colors — no two of a color move the same particle
/// — so a color's are projected in parallel on the task scheduler with the same result as on one thread.
///
/// The rigid bodies have been moved by the backend when soft bodies step: each moves, as the soft bodies see it, from
/// its pose before the rigid step to its pose after (interpolated at each sub-step). Particles touch colliders as spheres
/// of the body's particle radius, with Coulomb friction (the geometric mean of the body's and the collider's
/// coefficients); an overlap a sub-step starts with is undone without becoming velocity. A particle pinned to a
/// dynamic body, or touching one of its colliders, shares each correction with the body by their inverse masses at that
/// point (the body's from its mass and inertia): the body's part moves it within the step, as the soft body sees it, and
/// the backend then gives the body that motion and the impulse it means (corrections()) — momentum is exchanged both
/// ways, and the body ends where the soft bodies left it. Soft bodies do not collide with each other.
export module gfx.physics.dynamics:soft_body_solver;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.spatial;
import gfx.physics.tasks;
import gfx.physics.interface;

export namespace gfx::physics
{

/// @brief What does not change in a soft body after it is made: its particles' inverse masses and starting positions, its
///        constraints with their rest values, grouped in colors.
class SoftBodyModel
{
public:
    /// @brief A constraint ready to project.
    struct Constraint
    {
        SoftBodyConstraintKind kind = SoftBodyConstraintKind::STRETCH; ///< What it keeps.
        std::array<std::uint32_t, 4> particles{};                      ///< Two, or four for VOLUME and DIHEDRAL.
        float compliance = 0.0F;                                       ///< XPBD compliance.
        float rest = 0.0F;                                             ///< Its rest length, six times the volume, or angle.
    };

    /// @brief The model of @p description: rest values from its starting positions where not given.
    /// @throws PhysicsError if a constraint names a particle out of range or one particle twice.
    explicit SoftBodyModel(const SoftBodyDescription& description);

    /// @brief Its particles.
    [[nodiscard]] auto particle_count() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_inverse_masses.size());
    }

    /// @brief Their inverse masses.
    [[nodiscard]] auto inverse_masses() const noexcept -> std::span<const float>
    {
        return m_inverse_masses;
    }

    /// @brief Their starting positions (self-collision ignores particles that start within two radii).
    [[nodiscard]] auto rest_positions() const noexcept -> std::span<const Vector3>
    {
        return m_rest_positions;
    }

    /// @brief Its constraints but tethers, color after color.
    [[nodiscard]] auto constraints() const noexcept -> std::span<const Constraint>
    {
        return m_constraints;
    }

    /// @brief Where each color begins in constraints(), then where the last ends.
    [[nodiscard]] auto color_starts() const noexcept -> std::span<const std::uint32_t>
    {
        return m_color_starts;
    }

    /// @brief Its tethers, in order (each moves its first particle only: the second is its anchor).
    [[nodiscard]] auto tethers() const noexcept -> std::span<const Constraint>
    {
        return m_tethers;
    }

private:
    std::vector<float> m_inverse_masses{};
    std::vector<Vector3> m_rest_positions{};
    std::vector<Constraint> m_constraints{};
    std::vector<std::uint32_t> m_color_starts{};
    std::vector<Constraint> m_tethers{};
};

/// @brief A rigid body as soft bodies see it in a step: the backend has moved it from start_pose to end_pose.
struct CoupledBody
{
    RigidBodyHandle handle{};            ///< Null: a free slot (ignored).
    BodyKind kind = BodyKind::STATIC;    ///< Dynamic bodies take part of the corrections; the others are followed.
    Transform start_pose{};              ///< Its frame before the rigid step.
    Transform end_pose{};                ///< Its frame after it.
    MassProperties mass{};               ///< In effect (dynamic bodies), in its frame.
    MotionLock locks = MotionLock::NONE; ///< Axes it may not move along or about.
};

/// @brief A collider soft bodies may touch.
struct CoupledCollider
{
    ColliderHandle handle{};        ///< Null: a free slot (ignored).
    std::uint32_t body = 0;         ///< Its body, an index into the bodies.
    const Shape* shape = nullptr;   ///< Its shape (not null for live colliders).
    Transform pose{};               ///< In its body's frame.
    Friction friction{};            ///< Its material's.
    CollisionFilter filter{};       ///< What it collides with.
};

/// @brief A soft body in a step: its model, its settings, its particles (advanced in place).
struct SimulatedSoftBody
{
    const SoftBodyModel* model = nullptr;          ///< What it is made of.
    const SoftBodyDescription* settings = nullptr; ///< Pins, radius, damping, friction, gravity scale, sub-steps, self-collision, filter (its particles and constraints are the model's).
    std::span<Vector3> positions{};                ///< Its particles' positions.
    std::span<Vector3> velocities{};               ///< Their velocities.
};

/// @brief What soft bodies did to a dynamic body in a step: the impulse they gave it, and how far it moved as they saw it
///        (beyond the backend's motion), for the backend to give it both.
struct BodyCorrection
{
    RigidBodyHandle body{};       ///< The body.
    Vector3 linear_impulse{};     ///< Through its center of mass.
    Vector3 angular_impulse{};    ///< About its center of mass, world space.
    Vector3 translation{};        ///< Of its center of mass.
    Quaternion rotation{};        ///< About its center of mass (applied after its orientation).
};

/// @brief Steps soft bodies (XPBD) against the rigid bodies of a world.
class XpbdSoftBodySolver
{
public:
    XpbdSoftBodySolver();
    ~XpbdSoftBodySolver();

    XpbdSoftBodySolver(const XpbdSoftBodySolver&) = delete;
    auto operator=(const XpbdSoftBodySolver&) -> XpbdSoftBodySolver& = delete;

    /// @brief Advances @p soft_bodies by @p seconds under @p gravity, one after the other, against @p colliders on
    ///        @p bodies, their positions predicted by @p integrator, the large colors of their constraints on
    ///        @p scheduler's threads when given. The same inputs give the same bits on any number of threads.
    /// @throws PhysicsError if @p seconds is not finite and positive, a soft body has no model or its spans do not hold
    ///         its particles; what the integrator throws.
    void step(float seconds,
              Vector3 gravity,
              std::span<const SimulatedSoftBody> soft_bodies,
              std::span<const CoupledBody> bodies,
              std::span<const CoupledCollider> colliders,
              Integrator& integrator,
              TaskScheduler* scheduler);

    /// @brief What the last step's soft bodies did to dynamic bodies (one per body, in the bodies' order), to apply to
    ///        them now.
    [[nodiscard]] auto corrections() const noexcept -> std::span<const BodyCorrection>;

    /// @brief Contacts between particles and colliders in the last step's last sub-steps.
    [[nodiscard]] auto contact_count() const noexcept -> std::uint32_t;

    /// @brief What the solver keeps between steps (defined in its implementation unit).
    struct State;

private:
    std::unique_ptr<State> m_state;
};

} // namespace gfx::physics
