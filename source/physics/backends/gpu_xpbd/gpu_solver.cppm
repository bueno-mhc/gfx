// The GPU backend's internal partition of constraints: the layouts the solver's passes share with shaders/solver.glsl
// (each checked by size and offset), the joints' translation into the parts the GPU solves, and GpuConstraintSolver,
// which records a step's constraint work into the backend's command buffer — the colliders' instances and boxes, a moving
// tree rebuilt every step and a static tree rebuilt when static colliders change, the pairs worth a narrow phase (bodies
// joined without collisions left out), the manifolds, the contact cache, the contact constraints, islands and waking over
// contacts and joints, the body-to-constraint graph, the Jones-Plassmann coloring of joints and contacts together, the
// position projection and velocity pass of each substep, the joints' forces and breaking, and sleep.
module;

#include <stddef.h>

module gfx.physics.gpu_xpbd:solver;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.core.math;
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

/// The solver passes' parameters (SolverParameters in solver.glsl): the settings of the step, the counts and capacities,
/// the arrays' bindless indices. Constraints are numbered joints first (by joint slot), then contacts (by manifold slot).
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
    std::uint32_t graph_values = 0;       ///< values (constraint × 2 + side),
    std::uint32_t graph_sorted_keys = 0;  ///< sorted keys,
    std::uint32_t graph_sorted_values = 0; ///< sorted values,
    std::uint32_t body_begin = 0;         ///< each body's first entry,
    std::uint32_t body_end = 0;           ///< and one past its last,
    std::uint32_t colors_first = 0;       ///< the coloring's two buffers (a word per constraint),
    std::uint32_t colors_second = 0;      ///<
    std::uint32_t color_keys = 0;         ///< each constraint's color × 2 + its kind (0 joint, 1 contact),
    std::uint32_t color_values = 0;       ///< the constraints,
    std::uint32_t color_sorted_keys = 0;  ///< sorted by key,
    std::uint32_t color_lists = 0;        ///< the constraints by key,
    std::uint32_t color_begin = 0;        ///< each key's first,
    std::uint32_t color_end = 0;          ///< and one past its last,
    std::uint32_t labels = 0;             ///< the islands' labels (a word per body),
    std::uint32_t island_flags = 0;       ///< their flags (awake, kept awake),
    std::uint32_t island_timers = 0;      ///< their smallest sleep timers,
    std::uint32_t corrections = 0;        ///< the Jacobi corrections (four vectors per constraint),
    std::uint32_t joint_count = 0;        ///< Joint slots (the first constraints),
    std::uint32_t joined_count = 0;       ///< pairs of bodies joined without collisions,
    std::uint32_t joints = 0;             ///< the joints (GpuJoint, by slot),
    std::uint32_t joint_parts = 0;        ///< their parts (GpuJointPart),
    std::uint32_t joint_multipliers = 0;  ///< the multipliers of each part (a vector: up to three axes),
    std::uint32_t joint_impulses = 0;     ///< per joint, the step's impulses on the second body (two vectors: linear, angular),
    std::uint32_t joint_results = 0;      ///< per joint, the step's force and torque (two vectors; flags in the first's w),
    std::uint32_t joined_pairs = 0;       ///< the bodies joined without collisions (two words each, the smaller first, sorted).
};
static_assert(sizeof(GpuSolverParameters) == 220 && offsetof(GpuSolverParameters, colliders) == 60 && offsetof(GpuSolverParameters, counts) == 76 &&
              offsetof(GpuSolverParameters, corrections) == 184 && offsetof(GpuSolverParameters, joint_count) == 188 &&
              offsetof(GpuSolverParameters, joined_pairs) == 216);

/// The words a step's solver passes count (COUNT_* in solver.glsl), read back after each step.
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
    std::uint32_t graph_entries = 0;        ///< The graph's entries (two per constraint).
    std::uint32_t constraints = 0;          ///< Constraints of the step: the joint slots and the manifold slots.
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

/// A joint on the GPU (Joint in solver.glsl), by joint slot: its bodies, flags and parts; its frames about its bodies'
/// centers of mass, in their frames (the world's for no second body); its breaking thresholds.
struct GpuJoint
{
    std::uint32_t first = 0;                              ///< The first body's slot.
    std::uint32_t second = NO_SOLVER_BODY;                ///< The second's (NO_SOLVER_BODY: the world).
    std::uint32_t flags = 0;                              ///< GpuJointFlag::ACTIVE, or nothing.
    std::uint32_t part_count = 0;                         ///< Its parts,
    std::uint32_t first_part = 0;                         ///< from this one.
    std::array<std::uint32_t, 3> reserved{};              ///< (Alignment.)
    GpuVector first_position{};                           ///< Its frame on the first body; w: the breaking force.
    GpuVector first_orientation{0.0F, 0.0F, 0.0F, 1.0F};  ///<
    GpuVector second_position{};                          ///< Its frame on the second body; w: the breaking torque.
    GpuVector second_orientation{0.0F, 0.0F, 0.0F, 1.0F}; ///<
};
static_assert(sizeof(GpuJoint) == 96 && offsetof(GpuJoint, first_part) == 16 && offsetof(GpuJoint, first_position) == 32 &&
              offsetof(GpuJoint, second_orientation) == 80);

/// A part of a joint (JointPart in solver.glsl): one XPBD constraint of a GpuJointPartKind, about an axis of the first
/// frame, with bounds (or a motor's speed and force) and a spring.
struct GpuJointPart
{
    std::uint32_t kind = 0;                  ///< GpuJointPartKind.
    std::uint32_t axis = 0;                  ///< 0 x, 1 y, 2 z of the first frame (ALIGNED_AXIS, ANGLE, motors).
    std::array<std::uint32_t, 2> reserved{}; ///< (Alignment.)
    GpuVector lower{};                       ///< Lower bounds (ANCHORS: per axis; others: x; a motor's x: its speed); w: the
                                             ///< spring's frequency (0: rigid).
    GpuVector upper{};                       ///< Upper bounds (SWING's x: its limit; a motor's x: its largest force or
                                             ///< torque); w: the spring's damping ratio.
};
static_assert(sizeof(GpuJointPart) == 48 && offsetof(GpuJointPart, lower) == 16 && offsetof(GpuJointPart, upper) == 32);

/// What one step's constraint work needs.
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

// ===================================================================================================== joints

/// A joint's frames relative to its bodies' frames (the second's in the world for no second body), fixed when it is
/// created: its anchors and axes there.
struct JointFrames
{
    Transform first{};  ///< In the first body's frame.
    Transform second{}; ///< In the second body's frame (the world's for none).
};

/// The frames of @p description when its bodies are at @p first_pose and @p second_pose (the world's identity for no
/// second body), as the reference backend fixes them: a hinge's and a cone-twist's axis is their z, a slider's their x.
[[nodiscard]] auto joint_frames(const JointDescription& description,
                                const Transform& first_pose,
                                const Transform& second_pose) noexcept -> JointFrames;

/// Appends the parts the GPU solves @p description as (motors, springs, angular parts, then the anchors: the last
/// projected is the best kept).
void append_joint_parts(const JointDescription& description,
                        std::vector<GpuJointPart>& parts);

/// Whether @p description keeps its bodies from colliding.
[[nodiscard]] auto keeps_apart(const JointDescription& description) noexcept -> bool;

// ===================================================================================================== GpuConstraintSolver

/// A world's constraint work on the GPU: contacts and joints.
class GpuConstraintSolver
{
public:
    /// @param device       The device (it outlives the solver).
    /// @param capabilities What the backend found on it (the primitives' variants).
    /// @param settings     The backend's settings (solver mode, trees' layout).
    GpuConstraintSolver(Device& device,
                        const GpuXpbdCapabilities& capabilities,
                        const GpuXpbdDescription& settings);
    ~GpuConstraintSolver();

    GpuConstraintSolver(const GpuConstraintSolver&) = delete;
    auto operator=(const GpuConstraintSolver&) -> GpuConstraintSolver& = delete;

    /// The narrow phase (its shape table).
    [[nodiscard]] auto narrow_phase() noexcept -> GpuNarrowPhase&;

    /// Sets the colliders (by slot) and the lists of moving and static colliders' slots, uploaded by the next
    /// record_uploads(); @p static_changed: the static tree is rebuilt.
    void set_colliders(std::span<const GpuCollider> colliders,
                       std::span<const std::uint32_t> moving,
                       std::span<const std::uint32_t> statics,
                       bool static_changed);

    /// Sets the joints (by slot), their parts and the pairs of body slots joined without collisions (the smaller first,
    /// sorted), uploaded by the next record_uploads(). The joints' results of earlier steps are kept by slot.
    void set_joints(std::span<const GpuJoint> joints,
                    std::span<const GpuJointPart> parts,
                    std::span<const std::array<std::uint32_t, 2>> joined);

    /// The joint slots on the GPU.
    [[nodiscard]] auto joint_count() const noexcept -> std::uint32_t;

    /// Grows the arrays for @p body_count bodies and the capacities (copies recorded), uploads what changed (colliders,
    /// lists, joints, the shape table) and clears this step's counts and joint impulses. Recorded before the step's first
    /// barrier.
    void record_uploads(ComputeRecorder& recorder,
                        std::uint32_t body_count,
                        std::vector<std::unique_ptr<Buffer>>& retired);

    /// Records the step's detection, contact constraints, islands (waking) and coloring.
    void record_detection(ComputeRecorder& recorder,
                          const ContactStep& step);

    /// Records an iteration of the position projection, joints and contacts (@p first: the substep's first,
    /// multipliers reset).
    void record_positions(ComputeRecorder& recorder,
                          bool first);

    /// Records an iteration of the contacts' velocity pass (@p first: the substep's first, budgets reset).
    void record_velocities(ComputeRecorder& recorder,
                           bool first);

    /// Records the joints' forces and torques of the step (averaged over its substeps) and which broke.
    void record_joint_forces(ComputeRecorder& recorder);

    /// Records the sleep timers and the islands falling asleep.
    void record_sleep(ComputeRecorder& recorder);

    /// This step's counts (copied into the readback).
    [[nodiscard]] auto counts_buffer() const noexcept -> const Buffer&;

    /// The joints' results (two vectors per joint slot: the force with the flags' bits in w, the torque; copied into the
    /// readback).
    [[nodiscard]] auto joint_results_buffer() const noexcept -> const Buffer&;

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
