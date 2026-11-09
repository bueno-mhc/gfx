// The GPU backend's internal partition of soft bodies: the layouts its particles, soft bodies and soft constraints share
// with shaders/step.glsl and shaders/soft_solve.comp (each checked by size and offset), and GpuSoftBodies, which keeps
// their arrays and records their passes — a substep's prediction of the particles (gravity, rigid pins followed), the
// soft constraints color after color (their colors made once on the CPU), the velocity update with each soft body's
// damping. The particles are the solver's constraint nodes after the rigid bodies: contacts, pins (joints) and
// self-collision reach them through the solver (GpuConstraintSolver).
module;

#include <stddef.h>

module gfx.physics.gpu_xpbd:soft_bodies;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.profiler.graphics;
import gfx.physics.enums;
import :resources;

namespace gfx::physics::gpu_xpbd
{

// ===================================================================================================== shared layouts

/// What a particle keeps between edits (the particle_* helpers of step.glsl).
struct GpuParticle
{
    float inverse_mass = 0.0F;               ///< 0: it does not move by itself (fixed, or pinned rigidly).
    std::uint32_t flags = 0;                 ///< GpuBodyFlag: SIMULATED, AWAKE, and DYNAMIC or KINEMATIC (pinned rigidly to a point it follows).
    std::uint32_t soft_body = 0;             ///< Its soft body's index on the GPU.
    std::uint32_t pin_body = NO_SOLVER_BODY; ///< The body a KINEMATIC particle follows (NO_SOLVER_BODY: a point of the world).
    GpuVector rest{};                        ///< Its starting position (self-collision ignores pairs that start within two radii).
    GpuVector pin_point{};                   ///< The point it follows: in its body's frame about the center of mass, or in the world.
};
static_assert(sizeof(GpuParticle) == 48 && offsetof(GpuParticle, rest) == 16 && offsetof(GpuParticle, pin_point) == 32);

/// A soft body's settings on the GPU.
struct GpuSoftBody
{
    float gravity_scale = 1.0F;         ///< Times the world's gravity.
    float damping = 0.0F;               ///< Velocity lost per second.
    float radius = 0.0F;                ///< Of its particles.
    float friction = 0.0F;              ///< Against colliders.
    std::uint32_t iterations = 1;       ///< Of its constraints per substep (its sub-steps over the world's, rounded up).
    std::uint32_t flags = 0;            ///< Bit 0: its particles collide with each other.
    std::uint32_t first_particle = 0;   ///< Its particles,
    std::uint32_t particle_count = 0;   ///< so many from the first.
};
static_assert(sizeof(GpuSoftBody) == 32);

/// A soft constraint on the GPU (by color).
struct GpuSoftConstraint
{
    std::array<std::uint32_t, 4> particles{}; ///< Particle indices (two, or four for VOLUME and DIHEDRAL; a TETHER's second is its anchor).
    std::uint32_t kind = 0;                   ///< SoftBodyConstraintKind.
    float compliance = 0.0F;                  ///< XPBD compliance.
    float rest = 0.0F;                        ///< Rest length, six times the volume, or angle.
    std::uint32_t reserved = 0;               ///< (Alignment.)
};
static_assert(sizeof(GpuSoftConstraint) == 32 && offsetof(GpuSoftConstraint, kind) == 16);

/// The bit of GpuSoftBody::flags of a soft body whose particles collide with each other.
inline constexpr std::uint32_t GPU_SOFT_BODY_SELF_COLLISION = 1;

// ===================================================================================================== GpuSoftBodies

/// The soft bodies of a world on the GPU: their particles, settings and constraints.
class GpuSoftBodies
{
public:
    /// @param device The device (it outlives the soft bodies).
    explicit GpuSoftBodies(Device& device);
    ~GpuSoftBodies();

    GpuSoftBodies(const GpuSoftBodies&) = delete;
    auto operator=(const GpuSoftBodies&) -> GpuSoftBodies& = delete;

    /// Times its passes through @p timer.
    void set_timer(profiler::GpuTimer* timer) noexcept;

    /// Sets the particles' positions and velocities, uploaded by the next record_uploads() (what the GPU had is replaced).
    void set_state(std::span<const GpuVector> positions,
                   std::span<const GpuVector> velocities);

    /// Sets the particles and the soft bodies, uploaded by the next record_uploads().
    void set_particles(std::span<const GpuParticle> particles,
                       std::span<const GpuSoftBody> soft_bodies);

    /// Sets the soft constraints, color after color (@p color_starts: where each color begins, then where the last
    /// ends), uploaded by the next record_uploads().
    void set_constraints(std::span<const GpuSoftConstraint> constraints,
                         std::span<const std::uint32_t> color_starts);

    /// Sets the iterations of the soft constraints per substep: the most a soft body needs (the passes of the others skip
    /// what they do not need).
    void set_iterations(std::uint32_t iterations) noexcept;

    /// The particles on the GPU.
    [[nodiscard]] auto particle_count() const noexcept -> std::uint32_t;

    /// Grows the arrays and uploads what changed. Recorded before the step's first barrier.
    void record_uploads(ComputeRecorder& recorder,
                        std::vector<std::unique_ptr<Buffer>>& retired);

    /// Copies the particles' positions and velocities to their backups, or (@p restore) back.
    void record_backup(ComputeRecorder& recorder,
                       bool restore) const;

    /// Records a substep's prediction of the particles (after the bodies'): gravity, rigid pins followed.
    void record_integrate(ComputeRecorder& recorder,
                          std::uint32_t step_parameters) const;

    /// Records an iteration of the soft constraints (@p iteration of the substep: the multipliers reset at 0; soft
    /// bodies needing fewer skip it).
    void record_iteration(ComputeRecorder& recorder,
                          std::uint32_t step_parameters,
                          std::uint32_t iteration) const;

    /// The iterations of the soft constraints per substep (the most any soft body needs; 0 without constraints).
    [[nodiscard]] auto iterations() const noexcept -> std::uint32_t;

    /// Records a substep's velocity update of the particles (each soft body's damping).
    void record_velocity_update(ComputeRecorder& recorder,
                                std::uint32_t step_parameters) const;

    /// Bindless indices of the arrays the step parameters name.
    [[nodiscard]] auto positions() const noexcept -> const GpuArray&;
    [[nodiscard]] auto velocities() const noexcept -> const GpuArray&;
    [[nodiscard]] auto displacements() const noexcept -> const GpuArray&;
    [[nodiscard]] auto previous_velocities() const noexcept -> const GpuArray&;
    [[nodiscard]] auto particles() const noexcept -> const GpuArray&;
    [[nodiscard]] auto soft_bodies() const noexcept -> const GpuArray&;

    /// What only the implementation touches.
    struct State;

private:
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::physics::gpu_xpbd
