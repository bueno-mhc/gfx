// The GPU backend's internal partition of contacts: the layouts the contact passes share with shaders/solver.glsl (each
// checked by size and offset), and GpuContactSolver, which records a step's contact work into the backend's command
// buffer — the colliders' instances and boxes, a moving tree rebuilt every step and a static tree rebuilt when static
// colliders change, the pairs worth a narrow phase, the manifolds, the contact cache, the contact constraints, islands
// and waking, the body-to-constraint graph, the Jones-Plassmann coloring, the position projection and velocity pass of
// each substep, and sleep.
module;

#include <stddef.h>

module gfx.physics.gpu_xpbd:solver;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.interface;
import gfx.physics.narrow_phase;
import :settings;
import :resources;
import :primitives;
import :bvh;
import :narrow_phase;

namespace gfx::physics::gpu_xpbd
{

// ===================================================================================================== shared layouts

/// The contact passes' parameters (SolverParameters in solver.glsl): the settings of the step, the counts and
/// capacities, the arrays' bindless indices.
struct GpuSolverParameters
{
    float speculative_distance = 0.0F;    ///< Of the boxes and the narrow phase (the world's plus the step's motion).
    float contact_hertz = 0.0F;           ///< SolverDescription::contact_hertz (0: rigid contacts; the damping ratio is not
                                          ///< used: contact_solve.comp says why).
    float maximum_push_speed = 0.0F;      ///< SolverDescription::maximum_push_speed.
    float restitution_threshold = 0.0F;   ///< SolverDescription::restitution_threshold.
    float step_seconds = 0.0F;            ///< The step's duration.
    float sleep_linear_threshold = 0.0F;  ///< SleepDescription's.
    float sleep_angular_threshold = 0.0F; ///< SleepDescription's.
    float sleep_time = 0.0F;              ///< SleepDescription's.
    std::uint32_t sleep_enabled = 0;      ///< SleepDescription::enabled.
    std::uint32_t moving_count = 0;       ///< Colliders of the moving list (the first instances).
    std::uint32_t static_count = 0;       ///< Colliders of the static list (the instances after them).
    std::uint32_t within_capacity = 0;    ///< Pairs of the moving tree's own search at most.
    std::uint32_t between_capacity = 0;   ///< Pairs of the moving tree with the static tree at most.
    std::uint32_t manifold_capacity = 0;  ///< Manifold slots (contact constraints) at most.
    std::uint32_t body_count = 0;         ///< Body slots.
    std::uint32_t colliders = 0;          ///< Bindless indices: the colliders (GpuCollider, by slot),
    std::uint32_t collider_list = 0;      ///< the instances' colliders (the moving list, then the static one),
    std::uint32_t instances = 0;          ///< the instances (GpuInstance),
    std::uint32_t raw_pairs = 0;          ///< the trees' pairs (GpuPair: the within search, then the between search),
    std::uint32_t counts = 0;             ///< this step's counts (GpuSolverCounts),
    std::uint32_t previous_counts = 0;    ///< the previous step's,
    std::uint32_t pair_flags = 0;         ///< a word per pair slot: kept or not,
    std::uint32_t pair_indices = 0;       ///< the kept pair slots, compacted,
    std::uint32_t pairs = 0;              ///< the kept pairs' instances (GpuPair),
    std::uint32_t manifolds = 0;          ///< the manifolds (GpuManifold),
    std::uint32_t predecessors = 0;       ///< each manifold point's predecessor (four words per slot),
    std::uint32_t contacts = 0;           ///< the contact constraints (GpuContact),
    std::uint32_t previous_contacts = 0;  ///< the previous step's,
    std::uint32_t multipliers = 0;        ///< per slot: λ normal, λ tangential, friction used, rolling used (vectors),
    std::uint32_t graph_keys = 0;         ///< the graph's entries: keys (bodies),
    std::uint32_t graph_values = 0;       ///< values (slot × 2 + side),
    std::uint32_t graph_sorted_keys = 0;  ///< sorted keys,
    std::uint32_t graph_sorted_values = 0; ///< sorted values,
    std::uint32_t body_begin = 0;         ///< each body's first entry,
    std::uint32_t body_end = 0;           ///< and one past its last,
    std::uint32_t colors_first = 0;       ///< the coloring's two buffers,
    std::uint32_t colors_second = 0;      ///<
    std::uint32_t color_keys = 0;         ///< each slot's color (the overflow set, inactive),
    std::uint32_t color_values = 0;       ///< the slots,
    std::uint32_t color_sorted_keys = 0;  ///< sorted by color,
    std::uint32_t color_lists = 0;        ///< the slots by color,
    std::uint32_t color_begin = 0;        ///< each color's first,
    std::uint32_t color_end = 0;          ///< and one past its last,
    std::uint32_t labels = 0;             ///< the islands' labels (a word per body),
    std::uint32_t island_flags = 0;       ///< their flags (awake, kept awake),
    std::uint32_t island_timers = 0;      ///< their smallest sleep timers,
    std::uint32_t corrections = 0;        ///< the Jacobi corrections (four vectors per slot).
};
static_assert(sizeof(GpuSolverParameters) == 188 && offsetof(GpuSolverParameters, colliders) == 60 && offsetof(GpuSolverParameters, counts) == 76 &&
              offsetof(GpuSolverParameters, corrections) == 184);

/// The words a step's contact passes count (COUNT_* in solver.glsl), read back after each step.
struct GpuSolverCounts
{
    std::uint32_t within_pairs = 0;         ///< Pairs the moving tree's own search found.
    std::uint32_t between_pairs = 0;        ///< Pairs of the moving tree with the static tree.
    std::uint32_t candidates = 0;           ///< Of them, worth a narrow phase.
    std::uint32_t manifold_slots = 0;       ///< Slots the narrow phase needed.
    std::uint32_t colors = 0;               ///< Colors the coloring used.
    std::uint32_t overflow_constraints = 0; ///< Constraints without a color (Jacobi-style).
    std::uint32_t island_changes = 0;       ///< Labels the islands' last round changed (0: converged).
    std::uint32_t active_constraints = 0;   ///< Contact constraints.
    std::uint32_t contact_points = 0;       ///< Their points.
    std::uint32_t slots = 0;                ///< Manifold slots of the step (at most the capacity).
    std::uint32_t graph_entries = 0;        ///< The graph's entries (two per slot).
    std::uint32_t reserved = 0;             ///< (Alignment.)
};
static_assert(sizeof(GpuSolverCounts) == 48);

/// A collider on the GPU (Collider in solver.glsl).
struct GpuCollider
{
    GpuVector position{};                    ///< Relative to its body's center of mass, in the body's frame.
    GpuVector orientation{0.0F, 0.0F, 0.0F, 1.0F}; ///< In the body's frame.
    std::uint32_t body = 0;                  ///< Its body's slot.
    std::uint32_t shape = 0;                 ///< Its shape in the narrow phase's table.
    std::uint32_t flags = 0;                 ///< Bit 0: a live collider.
    std::int32_t group = 0;                  ///< CollisionFilter::group.
    std::array<std::uint32_t, 4> filter{};   ///< Category low and high words, mask low and high words.
    GpuVector material{};                    ///< Static friction, dynamic friction, rolling resistance, restitution.
};
static_assert(sizeof(GpuCollider) == 80 && offsetof(GpuCollider, body) == 32 && offsetof(GpuCollider, filter) == 48 && offsetof(GpuCollider, material) == 64);

/// The size of a contact constraint (GpuContact in solver.glsl: CONTACT_VECTORS vectors).
inline constexpr std::uint32_t GPU_CONTACT_BYTES = 19 * sizeof(GpuVector);

/// What one step's contact work needs.
struct ContactStep
{
    std::uint32_t step_parameters = 0;   ///< The step parameters' bindless index.
    std::uint32_t body_count = 0;        ///< Body slots.
    float step_seconds = 0.0F;           ///< The step's duration.
    float speculative_distance = 0.0F;   ///< The world's plus the step's motion.
    SolverDescription solver{};          ///< The world's solver settings.
    SleepDescription sleep{};            ///< The world's sleep settings.
    CollisionSettings collision{};       ///< The world's collision settings (the speculative distance replaced).
    bool lists_changed = false;          ///< The collider lists changed since the last completed step (instances name
                                         ///< other colliders: no contact keeps its previous anchors).
};

// ===================================================================================================== GpuContactSolver

/// A world's contact work on the GPU.
class GpuContactSolver
{
public:
    /// @param device       The device (it outlives the solver).
    /// @param capabilities What the backend found on it (the primitives' variants).
    /// @param settings     The backend's settings (solver mode, trees' layout).
    GpuContactSolver(Device& device,
                     const GpuXpbdCapabilities& capabilities,
                     const GpuXpbdDescription& settings);
    ~GpuContactSolver();

    GpuContactSolver(const GpuContactSolver&) = delete;
    auto operator=(const GpuContactSolver&) -> GpuContactSolver& = delete;

    /// The narrow phase (its shape table).
    [[nodiscard]] auto narrow_phase() noexcept -> GpuNarrowPhase&;

    /// Sets the colliders (by slot) and the lists of moving and static colliders' slots, uploaded by the next
    /// record_uploads(); @p static_changed: the static tree is rebuilt.
    void set_colliders(std::span<const GpuCollider> colliders,
                       std::span<const std::uint32_t> moving,
                       std::span<const std::uint32_t> statics,
                       bool static_changed);

    /// Grows the arrays for @p body_count bodies and the capacities (copies recorded), uploads what changed (colliders,
    /// lists, the shape table) and clears this step's counts. Recorded before the step's first barrier.
    void record_uploads(ComputeRecorder& recorder,
                        std::uint32_t body_count,
                        std::vector<std::unique_ptr<Buffer>>& retired);

    /// Records the step's detection, constraints, islands (waking) and coloring.
    void record_detection(ComputeRecorder& recorder,
                          const ContactStep& step);

    /// Records an iteration of the position projection (@p first: the substep's first, multipliers reset).
    void record_positions(ComputeRecorder& recorder,
                          bool first);

    /// Records an iteration of the velocity pass (@p first: the substep's first, budgets reset).
    void record_velocities(ComputeRecorder& recorder,
                           bool first);

    /// Records the sleep timers and the islands falling asleep.
    void record_sleep(ComputeRecorder& recorder);

    /// This step's counts (copied into the readback).
    [[nodiscard]] auto counts_buffer() const noexcept -> const Buffer&;

    /// Whether the step that counted @p counts overflowed a buffer or the recorded passes; if so the capacities grew (the
    /// step is replayed).
    [[nodiscard]] auto overflowed(const GpuSolverCounts& counts) -> bool;

    /// The step completed: its contacts become the previous ones, the static tree is current.
    void complete_step(const GpuSolverCounts& counts);

    /// What only the implementation touches.
    struct State;

private:
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::physics::gpu_xpbd
