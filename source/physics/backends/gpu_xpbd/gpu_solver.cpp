// GpuConstraintSolver: the constraint passes of a step recorded in order, their arrays grown with headroom from what
// earlier steps counted, the colliders, lists and joints uploaded when they change, the previous step's manifolds and
// contacts kept for the contact cache (two of each, swapped when a step completes). A GPU scope per group of passes
// (PROFILE_GPU_SCOPE through set_timer()'s timer): one per iteration of the solve, not per color.
module;

#include <gfx/profiler.hpp>

module gfx.physics.gpu_xpbd;

import std;
import gfx.profiler;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.interface;
import gfx.physics.narrow_phase;
import gfx.physics.gpu_xpbd.shaders;
import :settings;
import :resources;
import :primitives;
import :bvh;
import :narrow_phase;
import :solver;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The push constants of the solver's passes (Push in solver.glsl).
struct SolverPush
{
    std::uint32_t parameters = 0; ///< The step parameters' bindless index.
    std::uint32_t solver = 0;     ///< The solver parameters'.
    std::uint32_t argument = 0;   ///< The pass's: a color key, a round, a buffer.
    std::uint32_t mode = 0;       ///< The pass's mode bits.
};

/// The push constants of ranges.comp.
struct RangesPush
{
    std::uint32_t keys = 0;
    std::uint32_t begin_buffer = 0;
    std::uint32_t end_buffer = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    std::uint32_t mode = 0;
    std::uint32_t skipped = 0;
};

/// A sort key no range is made for (entries of no body).
constexpr std::uint32_t NO_KEY = 0xFFFFFFFFU;

/// The words of GpuSolverCounts (COUNT_* in solver.glsl).
enum CountWord : std::uint32_t
{
    WITHIN_PAIRS,
    BETWEEN_PAIRS,
    CANDIDATES,
    MANIFOLD_SLOTS,
    COLORS,
    OVERFLOW_CONSTRAINTS,
    ISLAND_CHANGES,
    ACTIVE_CONSTRAINTS,
    CONTACT_POINTS,
    SLOTS,
    GRAPH_ENTRIES,
    CONSTRAINTS,
    REPORTED,
    TOUCHING_POINTS,
    TRIGGER_MOVING_PAIRS,
    TRIGGER_STATIC_PAIRS,
    TRIGGER_CANDIDATES,
    TRIGGER_SLOTS,
    TRIGGER_OVERLAPS,
    OVERFLOW_ENTRIES,
    GATHER_CHUNKS,
    JOINT_COLORS,
    CONTACT_COLORS,
};

/// Bytes of the indirect dispatch arguments of a color key.
constexpr std::uint32_t DISPATCH_ARGUMENTS_BYTES = 3 * sizeof(std::uint32_t);

/// The color key of a color's joints (key × 2 + kind: joints 0, contacts 1; the overflow set is the color
/// MAXIMUM_GPU_XPBD_COLORS, inactive constraints the next).
[[nodiscard]] constexpr auto
joint_key(std::uint32_t color) noexcept -> std::uint32_t
{
    return 2 * color;
}

/// The color key of a color's contacts.
[[nodiscard]] constexpr auto
contact_key(std::uint32_t color) noexcept -> std::uint32_t
{
    return 2 * color + 1;
}

/// Color keys with dispatch arguments (the colors and the overflow set, both kinds), and all of them (inactive ones too).
constexpr std::uint32_t DISPATCHED_KEYS = 2 * (MAXIMUM_GPU_XPBD_COLORS + 1);
constexpr std::uint32_t COLOR_KEYS = 2 * (MAXIMUM_GPU_XPBD_COLORS + 2);

/// Pair or slot capacity for @p count with headroom (GPU_XPBD_CAPACITY_HEADROOM), at least 64.
[[nodiscard]] auto
with_headroom(std::uint32_t count) noexcept -> std::uint32_t
{
    const auto grown = static_cast<std::uint64_t>(static_cast<double>(count) * (1.0 + GPU_XPBD_CAPACITY_HEADROOM));
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(grown, 64, GPU_XPBD_MAXIMUM_ELEMENTS / 2));
}

/// The bits of a key below @p limit.
[[nodiscard]] constexpr auto
key_bits(std::uint32_t limit) noexcept -> std::uint32_t
{
    return std::max<std::uint32_t>(static_cast<std::uint32_t>(std::bit_width(limit)), 1);
}

} // namespace

// ===================================================================================================== State

struct GpuConstraintSolver::State
{
    State(Device& target,
          const GpuXpbdCapabilities& capabilities,
          const GpuXpbdDescription& description)
        : device{target}
        , settings{description}
        , primitives{target, capabilities}
        , narrow_phase{target, primitives}
        , moving_tree{target, primitives, {.layout = description.bvh_layout, .debug_name = "GPU physics: moving colliders"}}
        , static_tree{target, primitives, {.layout = description.bvh_layout, .debug_name = "GPU physics: static colliders"}}
        , trigger_tree{target, primitives, {.layout = description.bvh_layout, .debug_name = "GPU physics: triggers"}}
        , collider_poses{make_pipeline(target, shaders::collider_poses_comp, modules)}
        , pairs_filter{make_pipeline(target, shaders::pairs_filter_comp, modules)}
        , contact_prepare{make_pipeline(target, shaders::contact_prepare_comp, modules)}
        , islands{make_pipeline(target, shaders::islands_comp, modules)}
        , graph{make_pipeline(target, shaders::graph_comp, modules)}
        , ranges_pass{make_pipeline(target, shaders::ranges_comp, modules)}
        , coloring_pass{make_pipeline(target, shaders::color_comp, modules)}
        , contact_solve{make_pipeline(target, shaders::contact_solve_comp, modules)}
        , contact_velocity{make_pipeline(target, shaders::contact_velocity_comp, modules)}
        , joint_solve{make_pipeline(target, shaders::joint_solve_comp, modules)}
        , joint_forces{make_pipeline(target, shaders::joint_forces_comp, modules)}
        , jacobi_gather{make_pipeline(target, shaders::jacobi_gather_comp, modules)}
        , sleep{make_pipeline(target, shaders::sleep_comp, modules)}
        , contacts_report{make_pipeline(target, shaders::contacts_report_comp, modules)}
        , triggers_filter{make_pipeline(target, shaders::triggers_filter_comp, modules)}
    {
    }

    /// The colors whose joint passes and contact passes are recorded (RECORDED_PASSES_BY_USE: each kind's own).
    [[nodiscard]] auto joint_colors() const noexcept -> std::uint32_t
    {
        if (!coloring() || joint_count == 0)
        {
            return 0;
        }
        return has_all(settings.optimizations, GpuOptimization::RECORDED_PASSES_BY_USE) ? recorded_joint_colors : recorded_colors;
    }

    [[nodiscard]] auto contact_colors() const noexcept -> std::uint32_t
    {
        if (!coloring())
        {
            return 0;
        }
        return has_all(settings.optimizations, GpuOptimization::RECORDED_PASSES_BY_USE) ? recorded_contact_colors : recorded_colors;
    }

    /// Whether the overflow set's passes and gathers are recorded (always in the Jacobi mode).
    [[nodiscard]] auto overflow_recorded() const noexcept -> bool
    {
        return !coloring() || overflow_passes || !has_all(settings.optimizations, GpuOptimization::RECORDED_PASSES_BY_USE);
    }

    [[nodiscard]] auto coloring() const noexcept -> bool
    {
        return settings.solver_mode == GpuSolverMode::GRAPH_COLORING;
    }

    /// The trees' pair slots (the within search's, then the between search's).
    [[nodiscard]] auto candidates() const noexcept -> std::uint32_t
    {
        return within_capacity + between_capacity;
    }

    /// Constraints at most: the joint slots, then the manifold slots.
    [[nodiscard]] auto constraint_capacity() const noexcept -> std::uint32_t
    {
        return joint_count + manifold_capacity;
    }

    /// The word @p word of this step's counts.
    [[nodiscard]] auto count(CountWord word) const noexcept -> GpuSpan
    {
        return span_of(counts[parity].buffer(), word);
    }

    /// A pass of @p count invocations.
    void pass(ComputeRecorder& recorder,
              const ComputePipeline& pipeline,
              std::uint32_t argument,
              std::uint32_t mode,
              std::uint32_t count,
              const GlobalMemoryBarrier& barrier = COMPUTE_TO_COMPUTE) const
    {
        record_pass(recorder, pipeline, SolverPush{.parameters = step_parameters, .solver = solver_parameters.index(), .argument = argument, .mode = mode},
                    workgroups(std::max(count, 1U)), barrier);
    }

    /// A pass over the constraints of the color key @p key, dispatched by the arguments the coloring wrote; @p barrier:
    /// followed by the barrier to the next pass (a color's joints and contacts move different bodies: none between them).
    void color_pass(ComputeRecorder& recorder,
                    const ComputePipeline& pipeline,
                    std::uint32_t key,
                    std::uint32_t mode,
                    bool barrier = true) const
    {
        recorder.bind_pipeline(pipeline.handle());
        recorder.push_constants(SolverPush{.parameters = step_parameters, .solver = solver_parameters.index(), .argument = key, .mode = mode});
        recorder.dispatch_indirect(color_arguments.buffer().handle(), std::uint64_t{key} * DISPATCH_ARGUMENTS_BYTES);
        if (barrier)
        {
            recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_COMPUTE, 1}});
        }
    }

    /// With the overflow lists: the heavy nodes' chunks summed for the node pass of @p node_mode (0 positions, 1
    /// velocities, 2 shares), dispatched by the arguments the lists' building wrote.
    void gather_chunks(ComputeRecorder& recorder,
                       std::uint32_t node_mode) const
    {
        if (!gather_lists)
        {
            return;
        }
        recorder.bind_pipeline(jacobi_gather->handle());
        recorder.push_constants(SolverPush{.parameters = step_parameters, .solver = solver_parameters.index(), .argument = node_mode, .mode = 7});
        recorder.dispatch_indirect(chunk_arguments.buffer().handle(), 0);
        recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_COMPUTE, 1}});
    }

    /// The ranges of a sorted key list: @p maximum ranges cleared, then marked over the entries @p count_word counts.
    void ranges(ComputeRecorder& recorder,
                const GpuArray& sorted_keys,
                const GpuArray& begin,
                const GpuArray& end,
                std::uint32_t range_count,
                std::uint32_t maximum_entries,
                CountWord count_word) const
    {
        const RangesPush clear{.keys = sorted_keys.index(), .begin_buffer = begin.index(), .end_buffer = end.index(), .maximum = range_count, .mode = 0};
        record_pass(recorder, *ranges_pass, clear, workgroups(std::max(range_count, 1U)));
        const RangesPush mark{.keys = sorted_keys.index(),
                              .begin_buffer = begin.index(),
                              .end_buffer = end.index(),
                              .maximum = maximum_entries,
                              .count_buffer = counts[parity].index(),
                              .count_word = count_word,
                              .mode = 1,
                              .skipped = NO_KEY};
        record_pass(recorder, *ranges_pass, mark, workgroups(std::max(maximum_entries, 1U)));
    }

    Device& device;
    GpuXpbdDescription settings{};
    GpuPrimitives primitives;
    GpuNarrowPhase narrow_phase;
    GpuBvh moving_tree;
    GpuBvh static_tree;
    GpuBvh trigger_tree;
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::shared_ptr<ComputePipeline> collider_poses{};
    std::shared_ptr<ComputePipeline> pairs_filter{};
    std::shared_ptr<ComputePipeline> contact_prepare{};
    std::shared_ptr<ComputePipeline> islands{};
    std::shared_ptr<ComputePipeline> graph{};
    std::shared_ptr<ComputePipeline> ranges_pass{};
    std::shared_ptr<ComputePipeline> coloring_pass{};
    std::shared_ptr<ComputePipeline> contact_solve{};
    std::shared_ptr<ComputePipeline> contact_velocity{};
    std::shared_ptr<ComputePipeline> joint_solve{};
    std::shared_ptr<ComputePipeline> joint_forces{};
    std::shared_ptr<ComputePipeline> jacobi_gather{};
    std::shared_ptr<ComputePipeline> sleep{};
    std::shared_ptr<ComputePipeline> contacts_report{};
    std::shared_ptr<ComputePipeline> triggers_filter{};

    // Colliders and lists (CPU copies uploaded when they change).
    std::vector<GpuCollider> collider_data{};
    std::vector<std::uint32_t> lists{};
    std::uint32_t moving = 0;
    std::uint32_t statics = 0;
    std::uint32_t triggers = 0;
    bool colliders_pending = false;
    bool static_dirty = false;
    bool static_built = false;

    // Joints, their parts, the bodies joined without collisions (CPU copies uploaded when they change).
    std::vector<GpuJoint> joint_data{};
    std::vector<GpuJointPart> part_data{};
    std::vector<std::array<std::uint32_t, 2>> joined_data{};
    std::uint32_t joint_count = 0;
    std::uint32_t results_kept = 0; ///< Joint slots whose results earlier steps wrote.
    bool joints_pending = false;

    // Capacities, recorded passes, the current half of the double buffers.
    std::uint32_t body_count = 0;
    std::uint32_t node_count = 0;
    std::uint32_t within_capacity = 0;
    std::uint32_t between_capacity = 0;
    std::uint32_t pair_capacity = 0;
    std::uint32_t manifold_capacity = 0;
    std::uint32_t previous_capacity = 0;
    std::uint32_t recorded_colors = GPU_XPBD_INITIAL_COLORS;
    std::uint32_t recorded_joint_colors = GPU_XPBD_INITIAL_COLORS;   ///< RECORDED_PASSES_BY_USE: the joints' colors recorded.
    std::uint32_t recorded_contact_colors = GPU_XPBD_INITIAL_COLORS; ///< RECORDED_PASSES_BY_USE: the contacts' colors recorded.
    std::uint32_t previous_overflow = 0; ///< The overflow set's constraints in the last completed step.
    bool tree_refittable = false;        ///< BVH_REFIT: the moving tree was built over the moving colliders of now.
    std::uint32_t tree_age = 0;          ///< BVH_REFIT: steps since it was built.
    bool refitting = false;              ///< BVH_REFIT: this step refits it.
    bool overflow_passes = true;         ///< The overflow set's passes and gathers are recorded (RECORDED_PASSES_BY_USE: after a step that had one).
    bool gather_lists = false;           ///< This step's gathers read the overflow lists (CHUNKED_GATHER).
    std::uint32_t island_rounds = 8;
    std::uint32_t parity = 0;
    std::uint32_t step_parameters = 0;
    std::uint32_t report_capacity = 64;
    std::uint32_t trigger_within_capacity = 0;
    std::uint32_t trigger_between_capacity = 0;
    std::uint32_t trigger_pair_capacity = 0;
    std::uint32_t trigger_manifold_capacity = 0;
    GpuSolverParameters parameters{}; ///< As last written (rewritten when the reports grow after a step).
    CollisionSettings collision{};    ///< The step's (the triggers collide with them, without speculative distance).

    // Buffers.
    HostBuffer solver_parameters{"GPU physics: solver parameters", MemoryLocation::HOST_UPLOAD, BufferUsage::STORAGE};
    HostBuffer upload{"GPU physics: collider and joint upload", MemoryLocation::HOST_UPLOAD, BufferUsage::TRANSFER_SOURCE};
    GpuArray colliders{"GPU physics: colliders", sizeof(GpuCollider)};
    GpuArray collider_list{"GPU physics: collider lists", sizeof(std::uint32_t)};
    GpuArray instances{"GPU physics: instances", sizeof(GpuInstance)};
    GpuArray boxes{"GPU physics: boxes", 2 * sizeof(GpuVector)};
    GpuArray raw_pairs{"GPU physics: tree pairs", sizeof(GpuPair)};
    GpuArray pair_flags{"GPU physics: pair flags", sizeof(std::uint32_t)};
    GpuArray pair_indices{"GPU physics: kept pairs", sizeof(std::uint32_t)};
    GpuArray pairs{"GPU physics: pairs", sizeof(GpuPair)};
    GpuArray pair_ranges{"GPU physics: pair ranges", 2 * sizeof(std::uint32_t)};
    std::array<GpuArray, 2> manifolds{GpuArray{"GPU physics: manifolds", sizeof(GpuManifold)}, GpuArray{"GPU physics: manifolds", sizeof(GpuManifold)}};
    std::array<GpuArray, 2> counts{GpuArray{"GPU physics: counts", sizeof(GpuSolverCounts)}, GpuArray{"GPU physics: counts", sizeof(GpuSolverCounts)}};
    std::array<GpuArray, 2> contacts{GpuArray{"GPU physics: contacts", GPU_CONTACT_BYTES}, GpuArray{"GPU physics: contacts", GPU_CONTACT_BYTES}};
    GpuArray predecessors{"GPU physics: predecessors", 4 * sizeof(std::uint32_t)};
    GpuArray multipliers{"GPU physics: multipliers", 4 * sizeof(GpuVector)};
    GpuArray corrections{"GPU physics: Jacobi corrections", 4 * sizeof(GpuVector)};
    GpuArray graph_keys{"GPU physics: graph keys", sizeof(std::uint32_t)};
    GpuArray graph_values{"GPU physics: graph values", sizeof(std::uint32_t)};
    GpuArray graph_sorted_keys{"GPU physics: graph sorted keys", sizeof(std::uint32_t)};
    GpuArray graph_sorted_values{"GPU physics: graph sorted values", sizeof(std::uint32_t)};
    GpuArray body_begin{"GPU physics: body graph begins", sizeof(std::uint32_t)};
    GpuArray body_end{"GPU physics: body graph ends", sizeof(std::uint32_t)};
    GpuArray colors_first{"GPU physics: colors", sizeof(std::uint32_t)};
    GpuArray colors_second{"GPU physics: colors", sizeof(std::uint32_t)};
    GpuArray coloring_nodes{"GPU physics: coloring nodes", sizeof(GpuVector)};
    GpuArray reach_boxes{"GPU physics: reach boxes", 2 * sizeof(GpuVector)};
    GpuArray node_maxima{"GPU physics: coloring maxima", 8 * sizeof(std::uint32_t)};
    GpuArray color_keys{"GPU physics: color keys", sizeof(std::uint32_t)};
    GpuArray color_values{"GPU physics: color values", sizeof(std::uint32_t)};
    GpuArray color_sorted_keys{"GPU physics: color sorted keys", sizeof(std::uint32_t)};
    GpuArray color_lists{"GPU physics: color lists", sizeof(std::uint32_t)};
    GpuArray color_begin{"GPU physics: color begins", sizeof(std::uint32_t)};
    GpuArray color_end{"GPU physics: color ends", sizeof(std::uint32_t)};
    GpuArray color_arguments{"GPU physics: color dispatches", DISPATCH_ARGUMENTS_BYTES, BufferUsage::INDIRECT};
    GpuArray labels{"GPU physics: island labels", sizeof(std::uint32_t)};
    GpuArray island_flags{"GPU physics: island flags", sizeof(std::uint32_t)};
    GpuArray island_timers{"GPU physics: island timers", sizeof(std::uint32_t)};
    GpuArray joints{"GPU physics: joints", sizeof(GpuJoint)};
    GpuArray joint_parts{"GPU physics: joint parts", sizeof(GpuJointPart)};
    GpuArray joint_multipliers{"GPU physics: joint multipliers", sizeof(GpuVector)};
    GpuArray joint_impulses{"GPU physics: joint impulses", 2 * sizeof(GpuVector)};
    GpuArray joint_results{"GPU physics: joint results", 2 * sizeof(GpuVector)};
    GpuArray joined_pairs{"GPU physics: joined bodies", 2 * sizeof(std::uint32_t)};
    GpuArray overflow_counts{"GPU physics: overflow counts", 2 * sizeof(std::uint32_t)};
    GpuArray overflow_flags{"GPU physics: overflow entry flags", sizeof(std::uint32_t)};
    GpuArray overflow_positions{"GPU physics: overflow entry positions", sizeof(std::uint32_t)};
    GpuArray overflow_keys{"GPU physics: overflow entry keys", sizeof(std::uint32_t)};
    GpuArray overflow_values{"GPU physics: overflow entry values", sizeof(std::uint32_t)};
    GpuArray overflow_begin{"GPU physics: overflow begins", sizeof(std::uint32_t)};
    GpuArray overflow_end{"GPU physics: overflow ends", sizeof(std::uint32_t)};
    GpuArray chunk_counts{"GPU physics: gather chunk counts", sizeof(std::uint32_t)};
    GpuArray chunk_first{"GPU physics: gather first chunks", sizeof(std::uint32_t)};
    GpuArray chunk_descriptors{"GPU physics: gather chunks", 4 * sizeof(std::uint32_t)};
    GpuArray partials{"GPU physics: gather chunk sums", 2 * sizeof(GpuVector)};
    GpuArray chunk_arguments{"GPU physics: gather chunk dispatch", DISPATCH_ARGUMENTS_BYTES, BufferUsage::INDIRECT};
    GpuArray report_flags{"GPU physics: reported contacts' flags", sizeof(std::uint32_t)};
    GpuArray report_indices{"GPU physics: reported contacts", sizeof(std::uint32_t)};
    GpuArray reports{"GPU physics: contact reports", sizeof(GpuContactReport)};
    GpuArray trigger_raw_pairs{"GPU physics: trigger tree pairs", sizeof(GpuPair)};
    GpuArray trigger_pair_flags{"GPU physics: trigger pair flags", sizeof(std::uint32_t)};
    GpuArray trigger_pair_indices{"GPU physics: kept trigger pairs", sizeof(std::uint32_t)};
    GpuArray trigger_pairs{"GPU physics: trigger pairs", sizeof(GpuPair)};
    GpuArray trigger_ranges{"GPU physics: trigger pair ranges", 2 * sizeof(std::uint32_t)};
    GpuArray trigger_manifolds{"GPU physics: trigger manifolds", sizeof(GpuManifold)};
    GpuArray trigger_results{"GPU physics: trigger overlaps", sizeof(GpuTriggerOverlap)};

    PassTimer passes{}; ///< What its passes are timed through.
};

// ===================================================================================================== GpuConstraintSolver

GpuConstraintSolver::GpuConstraintSolver(Device& device,
                                         const GpuXpbdCapabilities& capabilities,
                                         const GpuXpbdDescription& settings)
    : m_state{std::make_unique<State>(device, capabilities, settings)}
{
}

GpuConstraintSolver::~GpuConstraintSolver() = default;

auto
GpuConstraintSolver::narrow_phase() noexcept -> GpuNarrowPhase&
{
    return m_state->narrow_phase;
}

auto
GpuConstraintSolver::primitives() noexcept -> GpuPrimitives&
{
    return m_state->primitives;
}

auto
GpuConstraintSolver::refitting() const noexcept -> bool
{
    return m_state->refitting;
}

void
GpuConstraintSolver::set_timer(profiler::GpuTimer* timer) noexcept
{
    State& state = *m_state;
    state.passes.set(timer);
    state.primitives.set_timer(timer);
    state.narrow_phase.set_timer(timer);
    for (GpuBvh* tree : {&state.moving_tree, &state.static_tree, &state.trigger_tree})
    {
        tree->set_timer(timer);
    }
}

void
GpuConstraintSolver::set_colliders(std::span<const GpuCollider> colliders,
                                   std::span<const std::uint32_t> moving,
                                   std::span<const std::uint32_t> statics,
                                   std::span<const std::uint32_t> triggers,
                                   bool static_changed)
{
    State& state = *m_state;
    // BVH_REFIT: the moving tree stays refittable while its leaves (the moving colliders, in order) are the same.
    if (state.moving != moving.size() || !std::ranges::equal(std::span{state.lists}.first(std::min<std::size_t>(state.moving, state.lists.size())), moving))
    {
        state.tree_refittable = false;
    }
    state.collider_data.assign(colliders.begin(), colliders.end());
    state.lists.assign(moving.begin(), moving.end());
    state.lists.insert(state.lists.end(), statics.begin(), statics.end());
    state.lists.insert(state.lists.end(), triggers.begin(), triggers.end());
    state.moving = static_cast<std::uint32_t>(moving.size());
    state.statics = static_cast<std::uint32_t>(statics.size());
    state.triggers = static_cast<std::uint32_t>(triggers.size());
    state.colliders_pending = true;
    state.static_dirty = state.static_dirty || static_changed;
}

void
GpuConstraintSolver::set_joints(std::span<const GpuJoint> joints,
                                std::span<const GpuJointPart> parts,
                                std::span<const std::array<std::uint32_t, 2>> joined)
{
    State& state = *m_state;
    state.joint_data.assign(joints.begin(), joints.end());
    state.part_data.assign(parts.begin(), parts.end());
    state.joined_data.assign(joined.begin(), joined.end());
    state.joint_count = static_cast<std::uint32_t>(joints.size());
    state.joints_pending = true;
}

auto
GpuConstraintSolver::joint_count() const noexcept -> std::uint32_t
{
    return m_state->joint_count;
}

void
GpuConstraintSolver::record_uploads(ComputeRecorder& recorder,
                                    std::uint32_t body_count,
                                    std::uint32_t node_count,
                                    std::vector<std::unique_ptr<Buffer>>& retired)
{
    // Capacities: what earlier steps counted (overflowed() grows them), at least room for the moving colliders. The kept
    // pairs and the manifold slots follow their own counts, not the trees' pairs: the trees' boxes grow by the step's
    // farthest motion, and most of their pairs are dropped by pairs_filter (a cloth's particles: millions).
    State& state = *m_state;
    state.body_count = body_count;
    state.node_count = node_count;
    state.within_capacity = std::max(state.within_capacity, with_headroom(2 * state.moving));
    state.between_capacity = std::max(state.between_capacity, with_headroom(2 * state.moving));
    state.pair_capacity = std::max(state.pair_capacity, with_headroom(2 * state.moving));
    state.manifold_capacity = std::max(state.manifold_capacity, state.pair_capacity);
    if (state.triggers > 0)
    {
        state.trigger_within_capacity = std::max(state.trigger_within_capacity, with_headroom(2 * state.triggers));
        state.trigger_between_capacity = std::max(state.trigger_between_capacity, with_headroom(2 * state.triggers));
        state.trigger_pair_capacity = std::max(state.trigger_pair_capacity, with_headroom(2 * state.triggers));
        state.trigger_manifold_capacity = std::max(state.trigger_manifold_capacity, state.trigger_pair_capacity);
    }
    const std::uint32_t candidates = state.candidates();
    const std::uint32_t slots = state.manifold_capacity;
    const std::uint32_t constraints = state.constraint_capacity();
    const std::uint32_t instances = state.moving + state.statics + state.triggers;
    const std::uint32_t trigger_candidates = state.trigger_within_capacity + state.trigger_between_capacity;
    const auto part_count = static_cast<std::uint32_t>(state.part_data.size());
    const auto joined_count = static_cast<std::uint32_t>(state.joined_data.size());

    // The components' room (before any of their commands is recorded).
    state.primitives.reserve(std::max({candidates, 2 * constraints, node_count, instances, trigger_candidates, GPU_XPBD_QUERY_PAIRS, 64U}));
    state.narrow_phase.reserve(std::max({state.pair_capacity, state.trigger_pair_capacity, GPU_XPBD_QUERY_PAIRS}),
                               std::max({slots, state.trigger_manifold_capacity, GPU_XPBD_QUERY_PIECES}));
    state.moving_tree.reserve(std::max(state.moving, 1U));
    state.static_tree.reserve(std::max(state.statics, 1U));
    state.trigger_tree.reserve(std::max(state.triggers, 1U));

    // The arrays (scratch ones need no copy; the previous step's halves and the joints' results keep their contents).
    Device& device = state.device;
    const auto grow = [&](GpuArray& array, std::uint32_t count, std::uint32_t kept = 0) { (void)array.reserve(device, recorder, std::max(count, 1U), kept, retired); };
    const std::uint32_t current = state.parity;
    const std::uint32_t previous = 1 - state.parity;
    grow(state.colliders, static_cast<std::uint32_t>(state.collider_data.size()));
    grow(state.collider_list, instances);
    if (has_all(state.settings.optimizations, GpuOptimization::REACH_BOXES))
    {
        grow(state.reach_boxes, instances);
    }
    grow(state.instances, instances + 1);
    grow(state.boxes, instances);
    for (GpuArray* array : {&state.raw_pairs, &state.pair_flags, &state.pair_indices})
    {
        grow(*array, candidates);
    }
    grow(state.pairs, state.pair_capacity);
    grow(state.pair_ranges, state.pair_capacity);
    grow(state.manifolds[current], slots);
    grow(state.manifolds[previous], slots, state.previous_capacity);
    grow(state.contacts[current], slots);
    grow(state.contacts[previous], slots, state.previous_capacity);
    grow(state.counts[current], 1);
    grow(state.counts[previous], 1, 1);
    grow(state.predecessors, slots);
    grow(state.multipliers, slots);
    grow(state.report_flags, slots);
    grow(state.report_indices, slots);
    grow(state.reports, state.report_capacity);
    for (GpuArray* array : {&state.trigger_raw_pairs, &state.trigger_pair_flags, &state.trigger_pair_indices})
    {
        grow(*array, trigger_candidates);
    }
    for (GpuArray* array : {&state.trigger_pairs, &state.trigger_ranges, &state.trigger_results})
    {
        grow(*array, state.trigger_pair_capacity);
    }
    grow(state.trigger_manifolds, state.trigger_manifold_capacity);
    for (GpuArray* array : {&state.corrections, &state.colors_first, &state.colors_second, &state.color_keys, &state.color_values, &state.color_sorted_keys,
                            &state.color_lists})
    {
        grow(*array, constraints);
    }
    for (GpuArray* array : {&state.graph_keys, &state.graph_values, &state.graph_sorted_keys, &state.graph_sorted_values})
    {
        grow(*array, 2 * constraints);
    }
    grow(state.body_begin, node_count);
    grow(state.body_end, node_count);
    if (has_all(state.settings.optimizations, GpuOptimization::NODE_MAXIMA_COLORING))
    {
        grow(state.coloring_nodes, constraints);
        grow(state.node_maxima, node_count);
    }
    grow(state.overflow_counts, node_count);
    if (has_all(state.settings.optimizations, GpuOptimization::CHUNKED_GATHER))
    {
        // The overflow lists: an entry per graph entry at most; chunks only for nodes of more than one (at most twice the
        // entries over the chunk size).
        for (GpuArray* array : {&state.overflow_flags, &state.overflow_positions, &state.overflow_keys, &state.overflow_values})
        {
            grow(*array, 2 * constraints);
        }
        for (GpuArray* array : {&state.overflow_begin, &state.overflow_end, &state.chunk_counts, &state.chunk_first})
        {
            grow(*array, node_count);
        }
        const std::uint32_t chunks = 2 * (2 * constraints) / GPU_XPBD_GATHER_CHUNK + 1;
        grow(state.chunk_descriptors, chunks);
        grow(state.partials, chunks);
        grow(state.chunk_arguments, 1);
    }
    for (GpuArray* array : {&state.labels, &state.island_flags, &state.island_timers})
    {
        grow(*array, body_count);
    }
    grow(state.color_begin, COLOR_KEYS);
    grow(state.color_end, COLOR_KEYS);
    grow(state.color_arguments, DISPATCHED_KEYS);
    grow(state.joints, state.joint_count);
    grow(state.joint_parts, part_count);
    grow(state.joint_multipliers, part_count);
    grow(state.joint_impulses, state.joint_count);
    grow(state.joint_results, state.joint_count, state.results_kept);
    grow(state.joined_pairs, joined_count);
    state.results_kept = std::max(state.results_kept, state.joint_count);
    state.solver_parameters.reserve(device, sizeof(GpuSolverParameters), retired);

    // What the growth copied and zeroed (here and in the backend's arrays, recorded before) lands before the uploads and
    // fills that write the same arrays.
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_TRANSFER, 1}});

    // The colliders and lists, the joints, their parts and the joined bodies when they changed, through one staging
    // buffer.
    struct Piece
    {
        std::span<const std::byte> bytes;
        const GpuArray* target;
    };
    std::vector<Piece> pieces;
    if (state.colliders_pending)
    {
        pieces.push_back({std::as_bytes(std::span{state.collider_data}), &state.colliders});
        pieces.push_back({std::as_bytes(std::span{state.lists}), &state.collider_list});
        state.colliders_pending = false;
    }
    if (state.joints_pending)
    {
        pieces.push_back({std::as_bytes(std::span{state.joint_data}), &state.joints});
        pieces.push_back({std::as_bytes(std::span{state.part_data}), &state.joint_parts});
        pieces.push_back({std::as_bytes(std::span{state.joined_data}), &state.joined_pairs});
        state.joints_pending = false;
    }
    std::uint64_t total = 0;
    for (const Piece& piece : pieces)
    {
        total += piece.bytes.size();
    }
    if (total > 0)
    {
        state.upload.reserve(device, total, retired);
        const std::span<std::byte> mapped = state.upload.bytes();
        std::uint64_t offset = 0;
        for (const Piece& piece : pieces)
        {
            if (piece.bytes.empty())
            {
                continue;
            }
            std::ranges::copy(piece.bytes, mapped.begin() + static_cast<std::ptrdiff_t>(offset));
            const std::array regions{BufferCopyRegion{.source_offset = offset, .size = piece.bytes.size()}};
            recorder.copy_buffer(state.upload.buffer().handle(), piece.target->buffer().handle(), regions);
            offset += piece.bytes.size();
        }
        state.upload.buffer().flush_mapped_range();
    }
    state.narrow_phase.upload_shapes(recorder);

    // This step's counts and joint impulses cleared.
    recorder.fill_buffer(state.counts[current].buffer().handle(), 0, WHOLE_SIZE, 0);
    recorder.fill_buffer(state.joint_impulses.buffer().handle(), 0, WHOLE_SIZE, 0);
}

void
GpuConstraintSolver::record_detection(ComputeRecorder& recorder,
                                      const ContactStep& step)
{
    // The parameters.
    State& state = *m_state;
    const std::uint32_t current = state.parity;
    const std::uint32_t previous = 1 - state.parity;
    const std::uint32_t slots = state.manifold_capacity;
    const std::uint32_t constraints = state.constraint_capacity();
    const std::uint32_t candidates = state.candidates();
    const std::uint32_t instances = state.moving + state.statics;
    const std::uint32_t posed = instances + state.triggers;
    state.step_parameters = step.step_parameters;
    state.gather_lists = has_all(state.settings.optimizations, GpuOptimization::CHUNKED_GATHER) && state.previous_overflow > 0;
    // The overflow lists' arrays exist only with CHUNKED_GATHER (else their slots name nothing: the shaders do not read them).
    const bool lists = has_all(state.settings.optimizations, GpuOptimization::CHUNKED_GATHER);
    const auto listed = [lists](const GpuArray& array) noexcept { return lists ? array.index() : 0U; };
    const bool maxima = has_all(state.settings.optimizations, GpuOptimization::NODE_MAXIMA_COLORING);
    const bool reach = has_all(state.settings.optimizations, GpuOptimization::REACH_BOXES);
    const GpuSolverParameters parameters{
        .speculative_distance = step.speculative_distance,
        .contact_hertz = step.solver.contact_hertz,
        .maximum_push_speed = step.solver.maximum_push_speed,
        .restitution_threshold = step.solver.restitution_threshold,
        .step_seconds = step.step_seconds,
        .sleep_linear_threshold = step.sleep.linear_threshold,
        .sleep_angular_threshold = step.sleep.angular_threshold,
        .sleep_time = step.sleep.time,
        .sleep_enabled = step.sleep.enabled ? 1U : 0U,
        .moving_count = state.moving,
        .static_count = state.statics,
        .within_capacity = state.within_capacity,
        .between_capacity = state.between_capacity,
        .manifold_capacity = slots,
        .body_count = step.body_count,
        .colliders = state.colliders.index(),
        .collider_list = state.collider_list.index(),
        .instances = state.instances.index(),
        .raw_pairs = state.raw_pairs.index(),
        .counts = state.counts[current].index(),
        .previous_counts = state.counts[previous].index(),
        .pair_flags = state.pair_flags.index(),
        .pair_indices = state.pair_indices.index(),
        .pairs = state.pairs.index(),
        .manifolds = state.manifolds[current].index(),
        .predecessors = state.predecessors.index(),
        .contacts = state.contacts[current].index(),
        .previous_contacts = state.contacts[previous].index(),
        .multipliers = state.multipliers.index(),
        .graph_keys = state.graph_keys.index(),
        .graph_values = state.graph_values.index(),
        .graph_sorted_keys = state.graph_sorted_keys.index(),
        .graph_sorted_values = state.graph_sorted_values.index(),
        .body_begin = state.body_begin.index(),
        .body_end = state.body_end.index(),
        .colors_first = state.colors_first.index(),
        .colors_second = state.colors_second.index(),
        .color_keys = state.color_keys.index(),
        .color_values = state.color_values.index(),
        .color_sorted_keys = state.color_sorted_keys.index(),
        .color_lists = state.color_lists.index(),
        .color_begin = state.color_begin.index(),
        .color_end = state.color_end.index(),
        .labels = state.labels.index(),
        .island_flags = state.island_flags.index(),
        .island_timers = state.island_timers.index(),
        .corrections = state.corrections.index(),
        .joint_count = state.joint_count,
        .joined_count = static_cast<std::uint32_t>(state.joined_data.size()),
        .joints = state.joints.index(),
        .joint_parts = state.joint_parts.index(),
        .joint_multipliers = state.joint_multipliers.index(),
        .joint_impulses = state.joint_impulses.index(),
        .joint_results = state.joint_results.index(),
        .joined_pairs = state.joined_pairs.index(),
        .node_count = step.node_count,
        .overflow_counts = state.overflow_counts.index(),
        .world_speculative_distance = step.collision.speculative_distance,
        .boxes = state.boxes.index(),
        .pair_capacity = state.pair_capacity,
        .report_capacity = state.report_capacity,
        .report_flags = state.report_flags.index(),
        .report_indices = state.report_indices.index(),
        .reports = state.reports.index(),
        .trigger_count = state.triggers,
        .trigger_within_capacity = state.trigger_within_capacity,
        .trigger_between_capacity = state.trigger_between_capacity,
        .trigger_pair_capacity = state.trigger_pair_capacity,
        .trigger_manifold_capacity = state.trigger_manifold_capacity,
        .trigger_raw_pairs = state.trigger_raw_pairs.index(),
        .trigger_pair_flags = state.trigger_pair_flags.index(),
        .trigger_pair_indices = state.trigger_pair_indices.index(),
        .trigger_pairs = state.trigger_pairs.index(),
        .trigger_ranges = state.trigger_ranges.index(),
        .trigger_manifolds = state.trigger_manifolds.index(),
        .trigger_results = state.trigger_results.index(),
        .tables = state.narrow_phase.table_index(),
        .overflow_flags = listed(state.overflow_flags),
        .overflow_positions = listed(state.overflow_positions),
        .overflow_keys = listed(state.overflow_keys),
        .overflow_values = listed(state.overflow_values),
        .overflow_begin = listed(state.overflow_begin),
        .overflow_end = listed(state.overflow_end),
        .chunk_counts = listed(state.chunk_counts),
        .chunk_first = listed(state.chunk_first),
        .chunk_descriptors = listed(state.chunk_descriptors),
        .partials = listed(state.partials),
        .gather_lists = state.gather_lists ? 1U : 0U,
        .coloring_nodes = maxima ? state.coloring_nodes.index() : 0U,
        .node_maxima = maxima ? state.node_maxima.index() : 0U,
        .maxima_coloring = maxima ? 1U : 0U,
        .reach_boxes = reach ? state.reach_boxes.index() : 0U,
    };
    state.parameters = parameters;
    state.collision = step.collision;
    std::memcpy(state.solver_parameters.bytes().data(), &parameters, sizeof(parameters));
    state.solver_parameters.buffer().flush_mapped_range();

    // Instances and boxes; the moving tree, the static tree when its colliders changed; their pairs.
    PROFILE_NAMED_SCOPE(broad_scope, "GPU XPBD: record the broad phase");
    if (step.timer != nullptr)
    {
        step.timer->mark(recorder, StepPhase::BROAD_PHASE);
    }
    if (instances > 0)
    {
        {
            PROFILE_GPU_SCOPE("collider_poses", recorder, state.passes);
            state.pass(recorder, *state.collider_poses, 0, 0, posed);
        }
        state.narrow_phase.bounds(recorder, span_of(state.instances.buffer()), span_of(state.boxes.buffer()), {.maximum = instances}, step.speculative_distance);
        if (reach)
        {
            // The trees' boxes: each grown by its own reach (REACH_BOXES); the leaves placed by the common boxes.
            PROFILE_GPU_SCOPE("reach boxes", recorder, state.passes);
            state.pass(recorder, *state.pairs_filter, 0, 2, instances);
        }
    }
    const GpuArray& tree_boxes = reach ? state.reach_boxes : state.boxes;
    if (state.static_dirty)
    {
        state.static_built = false;
        if (state.statics > 0)
        {
            state.static_tree.build(recorder, span_of(tree_boxes.buffer(), state.moving), span_of(state.boxes.buffer(), state.moving), {.maximum = state.statics});
            state.static_built = true;
        }
    }
    state.refitting = has_all(state.settings.optimizations, GpuOptimization::BVH_REFIT) && state.tree_refittable && state.tree_age < GPU_XPBD_REFIT_STEPS;
    if (state.moving > 0)
    {
        if (state.refitting)
        {
            // BVH_REFIT: the same leaves, their boxes of this step.
            state.moving_tree.refit(recorder, span_of(tree_boxes.buffer()));
        }
        else
        {
            state.moving_tree.build(recorder, span_of(tree_boxes.buffer()), span_of(state.boxes.buffer()), {.maximum = state.moving});
        }
        if (state.moving > 1)
        {
            state.moving_tree.find_pairs(recorder, span_of(state.raw_pairs.buffer()), state.count(WITHIN_PAIRS), state.within_capacity);
        }
        if (state.static_built)
        {
            state.moving_tree.find_pairs(recorder, state.static_tree, span_of(state.raw_pairs.buffer(), state.within_capacity), state.count(BETWEEN_PAIRS),
                                         state.between_capacity);
        }
    }

    broad_scope.end();

    // The pairs worth a narrow phase, compacted in order; their manifolds; each point's predecessor.
    PROFILE_NAMED_SCOPE(narrow_scope, "GPU XPBD: record the narrow phase");
    {
        PROFILE_GPU_SCOPE("pairs_filter", recorder, state.passes);
        state.pass(recorder, *state.pairs_filter, 0, 0, candidates);
        state.primitives.compact(recorder, span_of(state.pair_flags.buffer()), {}, span_of(state.pair_indices.buffer()), state.count(CANDIDATES), {.maximum = candidates});
        state.pass(recorder, *state.pairs_filter, 0, 1, candidates);
    }
    if (step.timer != nullptr)
    {
        step.timer->mark(recorder, StepPhase::NARROW_PHASE);
    }
    CollisionSettings collision = step.collision;
    collision.speculative_distance = step.speculative_distance;
    state.narrow_phase.collide(recorder, span_of(state.instances.buffer()), span_of(state.pairs.buffer()), {.maximum = state.pair_capacity, .word = state.count(CANDIDATES)},
                               {.manifolds = span_of(state.manifolds[current].buffer()),
                                .ranges = span_of(state.pair_ranges.buffer()),
                                .total = state.count(MANIFOLD_SLOTS),
                                .capacity = slots},
                               collision);
    const bool keeps_anchors = step.solver.warm_starting && !step.lists_changed && state.previous_capacity > 0;
    if (keeps_anchors)
    {
        state.narrow_phase.match(recorder, span_of(state.manifolds[previous].buffer()),
                                 {.maximum = state.previous_capacity, .word = span_of(state.counts[previous].buffer(), MANIFOLD_SLOTS)},
                                 span_of(state.manifolds[current].buffer()), {.maximum = slots, .word = state.count(MANIFOLD_SLOTS)},
                                 span_of(state.predecessors.buffer()));
    }

    narrow_scope.end();

    // The contact constraints (and the step's counts of constraints).
    PROFILE_NAMED_SCOPE(constraints_scope, "GPU XPBD: record the constraints, islands, graph and colors");
    {
        PROFILE_GPU_SCOPE("contact_prepare", recorder, state.passes);
        state.pass(recorder, *state.contact_prepare, 0, keeps_anchors ? 0U : 1U, slots);
    }

    // Islands over contacts and joints: labels hooked and compressed (the last round counts its changes), awake islands,
    // kinematic touches, bodies woken.
    if (step.timer != nullptr)
    {
        step.timer->mark(recorder, StepPhase::SOLVER);
    }
    {
        PROFILE_GPU_SCOPE("islands", recorder, state.passes);
        state.pass(recorder, *state.islands, 0, 0, step.body_count);
        for (std::uint32_t round = 0; round < state.island_rounds; ++round)
        {
            state.pass(recorder, *state.islands, round + 1 == state.island_rounds ? 1U : 0U, 1, constraints);
            state.pass(recorder, *state.islands, 0, 2, step.body_count);
        }
        state.pass(recorder, *state.islands, 0, 3, step.body_count);
        state.pass(recorder, *state.islands, 0, 4, constraints);
        state.pass(recorder, *state.islands, 0, 5, step.body_count);
    }

    // The graph: each body's constraints in order (joints, then contacts).
    {
        PROFILE_GPU_SCOPE("graph", recorder, state.passes);
        state.pass(recorder, *state.graph, 0, 0, constraints);
        state.primitives.sort(recorder, span_of(state.graph_keys.buffer()), span_of(state.graph_values.buffer()), span_of(state.graph_sorted_keys.buffer()),
                              span_of(state.graph_sorted_values.buffer()), {.maximum = 2 * constraints, .word = state.count(GRAPH_ENTRIES)});
        PROFILE_GPU_SCOPE("ranges", recorder, state.passes);
        state.ranges(recorder, state.graph_sorted_keys, state.body_begin, state.body_end, step.node_count, 2 * constraints, GRAPH_ENTRIES);
    }

    // The colors (the overflow set only, in the Jacobi mode), the lists by color and kind, their dispatch arguments.
    PROFILE_GPU_SCOPE("coloring", recorder, state.passes);
    const bool coloring = state.coloring();
    {
        PROFILE_GPU_SCOPE("color", recorder, state.passes);
        state.pass(recorder, *state.coloring_pass, coloring ? 0U : 1U, 0, constraints);
        const std::uint32_t rounds = coloring ? GPU_XPBD_COLORING_ROUNDS : 0;
        if (has_all(state.settings.optimizations, GpuOptimization::NODE_MAXIMA_COLORING))
        {
            // The bodies' maxima (NODE_MAXIMA_COLORING): the first round's priorities, then per round the ties' indices
            // and the choices (which also take the next round's priorities); the colors stay in the first buffer.
            if (rounds > 0)
            {
                state.pass(recorder, *state.coloring_pass, 0, 4, constraints);
            }
            for (std::uint32_t round = 0; round < rounds; ++round)
            {
                state.pass(recorder, *state.coloring_pass, round, 5, constraints);
                state.pass(recorder, *state.coloring_pass, round, 6, constraints);
            }
            state.pass(recorder, *state.coloring_pass, 0, 2, constraints);
        }
        else
        {
            for (std::uint32_t round = 0; round < rounds; ++round)
            {
                state.pass(recorder, *state.coloring_pass, round, 1, constraints);
            }
            state.pass(recorder, *state.coloring_pass, rounds % 2, 2, constraints);
        }
    }
    if (state.gather_lists)
    {
        // The overflow lists (CHUNKED_GATHER): the graph's overflow entries compacted in order, each node's range of
        // them, the heavy nodes' chunks and their dispatch.
        PROFILE_GPU_SCOPE("overflow lists", recorder, state.passes);
        const std::uint32_t entries = 2 * constraints;
        state.pass(recorder, *state.jacobi_gather, 0, 3, entries);
        state.primitives.compact(recorder, span_of(state.overflow_flags.buffer()), {}, span_of(state.overflow_positions.buffer()), state.count(OVERFLOW_ENTRIES),
                                 {.maximum = entries, .word = state.count(GRAPH_ENTRIES)});
        state.pass(recorder, *state.jacobi_gather, 0, 4, entries);
        state.ranges(recorder, state.overflow_keys, state.overflow_begin, state.overflow_end, step.node_count, entries, OVERFLOW_ENTRIES);
        state.pass(recorder, *state.jacobi_gather, 0, 5, step.node_count);
        state.primitives.exclusive_scan(recorder, span_of(state.chunk_counts.buffer()), span_of(state.chunk_first.buffer()), {.maximum = step.node_count},
                                        state.count(GATHER_CHUNKS));
        state.pass(recorder, *state.jacobi_gather, 0, 6, step.node_count);
        state.primitives.dispatch_arguments(recorder, {.maximum = state.chunk_descriptors.capacity(), .word = state.count(GATHER_CHUNKS)},
                                            span_of(state.chunk_arguments.buffer()));
    }
    if (state.overflow_recorded())
    {
        PROFILE_GPU_SCOPE("jacobi_gather (counts)", recorder, state.passes);
        state.gather_chunks(recorder, 2);
        state.pass(recorder, *state.jacobi_gather, 0, 2, step.node_count);
    }
    state.primitives.sort(recorder, span_of(state.color_keys.buffer()), span_of(state.color_values.buffer()), span_of(state.color_sorted_keys.buffer()),
                          span_of(state.color_lists.buffer()), {.maximum = constraints, .word = state.count(CONSTRAINTS)}, key_bits(COLOR_KEYS - 1));
    {
        PROFILE_GPU_SCOPE("ranges", recorder, state.passes);
        state.ranges(recorder, state.color_sorted_keys, state.color_begin, state.color_end, COLOR_KEYS, constraints, CONSTRAINTS);
    }
    PROFILE_GPU_SCOPE("color dispatch arguments", recorder, state.passes);
    state.pass(recorder, *state.coloring_pass, state.color_arguments.index(), 3, DISPATCHED_KEYS, COMPUTE_TO_INDIRECT);
}

void
GpuConstraintSolver::record_positions(ComputeRecorder& recorder,
                                      bool first)
{
    // The colors one after the other (Gauss-Seidel; a color's joints and contacts together), then the overflow set
    // (Jacobi) and its gather.
    const State& state = *m_state;
    PROFILE_GPU_SCOPE("position iteration", recorder, state.passes);
    const std::uint32_t reset = first ? 1U : 0U;
    const std::uint32_t joint_colors = state.joint_colors();
    const std::uint32_t contact_colors = state.contact_colors();
    const bool joints = state.joint_count > 0;
    for (std::uint32_t color = 0; color < std::max(joint_colors, contact_colors); ++color)
    {
        const bool contacts = color < contact_colors;
        if (color < joint_colors)
        {
            state.color_pass(recorder, *state.joint_solve, joint_key(color), reset, !contacts);
        }
        if (contacts)
        {
            state.color_pass(recorder, *state.contact_solve, contact_key(color), reset);
        }
    }
    if (!state.overflow_recorded())
    {
        return;
    }
    if (joints)
    {
        state.color_pass(recorder, *state.joint_solve, joint_key(MAXIMUM_GPU_XPBD_COLORS), reset | 2U, false);
    }
    state.color_pass(recorder, *state.contact_solve, contact_key(MAXIMUM_GPU_XPBD_COLORS), reset | 2U);
    PROFILE_GPU_SCOPE("jacobi_gather", recorder, state.passes);
    state.gather_chunks(recorder, 0);
    state.pass(recorder, *state.jacobi_gather, 0, 0, state.node_count);
}

void
GpuConstraintSolver::record_velocities(ComputeRecorder& recorder,
                                       bool first)
{
    // Contacts only: joints have no velocity pass.
    const State& state = *m_state;
    PROFILE_GPU_SCOPE("velocity iteration", recorder, state.passes);
    const std::uint32_t reset = first ? 1U : 0U;
    const std::uint32_t colors = state.contact_colors();
    for (std::uint32_t color = 0; color < colors; ++color)
    {
        state.color_pass(recorder, *state.contact_velocity, contact_key(color), reset);
    }
    if (!state.overflow_recorded())
    {
        return;
    }
    state.color_pass(recorder, *state.contact_velocity, contact_key(MAXIMUM_GPU_XPBD_COLORS), reset | 2U);
    PROFILE_GPU_SCOPE("jacobi_gather", recorder, state.passes);
    state.gather_chunks(recorder, 1);
    state.pass(recorder, *state.jacobi_gather, 0, 1, state.node_count);
}

void
GpuConstraintSolver::record_joint_forces(ComputeRecorder& recorder)
{
    const State& state = *m_state;
    if (state.joint_count > 0)
    {
        PROFILE_GPU_SCOPE("joint_forces", recorder, state.passes);
        state.pass(recorder, *state.joint_forces, 0, 0, state.joint_count);
    }
}

void
GpuConstraintSolver::record_sleep(ComputeRecorder& recorder)
{
    const State& state = *m_state;
    PROFILE_GPU_SCOPE("sleep", recorder, state.passes);
    state.pass(recorder, *state.sleep, 0, 0, state.body_count);
    state.pass(recorder, *state.sleep, 0, 1, state.body_count);
}

void
GpuConstraintSolver::record_reports(ComputeRecorder& recorder)
{
    // The touching contacts' slots flagged, compacted in slot order, their reports written.
    State& state = *m_state;
    PROFILE_GPU_SCOPE("contacts_report", recorder, state.passes);
    state.pass(recorder, *state.contacts_report, 0, 0, state.manifold_capacity);
    state.primitives.compact(recorder, span_of(state.report_flags.buffer()), {}, span_of(state.report_indices.buffer()), state.count(REPORTED),
                             {.maximum = state.manifold_capacity});
    state.pass(recorder, *state.contacts_report, 0, 1, state.report_capacity);
}

void
GpuConstraintSolver::record_reports_again(ComputeRecorder& recorder,
                                          std::uint32_t reported,
                                          std::vector<std::unique_ptr<Buffer>>& retired)
{
    // Room for them (with headroom, kept for the next steps), the parameters naming the grown reports.
    State& state = *m_state;
    state.report_capacity = std::max(state.report_capacity, with_headroom(reported));
    (void)state.reports.reserve(state.device, recorder, state.report_capacity, 0, retired);
    state.parameters.report_capacity = state.report_capacity;
    state.parameters.reports = state.reports.index();
    std::memcpy(state.solver_parameters.bytes().data(), &state.parameters, sizeof(state.parameters));
    state.solver_parameters.buffer().flush_mapped_range();
    recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});
    state.pass(recorder, *state.contacts_report, 0, 1, state.report_capacity);
}

auto
GpuConstraintSolver::report_capacity() const noexcept -> std::uint32_t
{
    return m_state->report_capacity;
}

void
GpuConstraintSolver::record_triggers(ComputeRecorder& recorder)
{
    State& state = *m_state;
    if (state.triggers == 0)
    {
        return;
    }

    // Every instance at its final pose, their boxes (no growth); the moving tree rebuilt from them, the triggers' tree.
    PROFILE_GPU_SCOPE("triggers", recorder, state.passes);
    const std::uint32_t colliders = state.moving + state.statics;
    state.pass(recorder, *state.collider_poses, 0, 0, colliders + state.triggers);
    state.narrow_phase.bounds(recorder, span_of(state.instances.buffer()), span_of(state.boxes.buffer()), {.maximum = colliders + state.triggers});
    if (state.moving > 0)
    {
        state.moving_tree.build(recorder, span_of(state.boxes.buffer()), {.maximum = state.moving});
    }
    state.trigger_tree.build(recorder, span_of(state.boxes.buffer(), colliders), {.maximum = state.triggers});

    // Their pairs with the moving tree and the static tree; kept, compacted, gathered; collided without speculative
    // distance; the overlapping ones appended to the results.
    if (state.moving > 0)
    {
        state.trigger_tree.find_pairs(recorder, state.moving_tree, span_of(state.trigger_raw_pairs.buffer()), state.count(TRIGGER_MOVING_PAIRS),
                                      state.trigger_within_capacity);
    }
    if (state.static_built)
    {
        state.trigger_tree.find_pairs(recorder, state.static_tree, span_of(state.trigger_raw_pairs.buffer(), state.trigger_within_capacity),
                                      state.count(TRIGGER_STATIC_PAIRS), state.trigger_between_capacity);
    }
    const std::uint32_t raw = state.trigger_within_capacity + state.trigger_between_capacity;
    PROFILE_GPU_SCOPE("triggers_filter", recorder, state.passes);
    state.pass(recorder, *state.triggers_filter, 0, 0, raw);
    state.primitives.compact(recorder, span_of(state.trigger_pair_flags.buffer()), {}, span_of(state.trigger_pair_indices.buffer()), state.count(TRIGGER_CANDIDATES),
                             {.maximum = raw});
    state.pass(recorder, *state.triggers_filter, 0, 1, state.trigger_pair_capacity);
    CollisionSettings touching = state.collision;
    touching.speculative_distance = 0.0F;
    state.narrow_phase.collide(recorder, span_of(state.instances.buffer()), span_of(state.trigger_pairs.buffer()),
                               {.maximum = state.trigger_pair_capacity, .word = state.count(TRIGGER_CANDIDATES)},
                               {.manifolds = span_of(state.trigger_manifolds.buffer()),
                                .ranges = span_of(state.trigger_ranges.buffer()),
                                .total = state.count(TRIGGER_SLOTS),
                                .capacity = state.trigger_manifold_capacity},
                               touching);
    state.pass(recorder, *state.triggers_filter, 0, 2, state.trigger_pair_capacity);
}

void
GpuConstraintSolver::record_query_tree(ComputeRecorder& recorder)
{
    State& state = *m_state;
    const std::uint32_t colliders = state.moving + state.statics;
    if (colliders == 0)
    {
        return;
    }
    state.pass(recorder, *state.collider_poses, 0, 0, colliders + state.triggers);
    state.narrow_phase.bounds(recorder, span_of(state.instances.buffer()), span_of(state.boxes.buffer()), {.maximum = colliders});
    if (state.moving > 0)
    {
        state.moving_tree.build(recorder, span_of(state.boxes.buffer()), {.maximum = state.moving});
    }
}

auto
GpuConstraintSolver::query_layout() const -> GpuQueryLayout
{
    const State& state = *m_state;
    const bool moving = state.moving > 0;
    const bool statics = state.statics > 0 && state.static_built;
    const auto index = [](const Buffer& buffer) { return buffer.bindless_index().value(); };
    return {.tables = state.narrow_phase.table_index(),
            .colliders = state.colliders.index(),
            .collider_list = state.collider_list.index(),
            .instances = state.instances.index(),
            .boxes = state.boxes.index(),
            .moving_header = moving ? index(state.moving_tree.header_buffer()) : 0,
            .moving_nodes = moving ? index(state.moving_tree.node_buffer()) : 0,
            .static_header = statics ? index(state.static_tree.header_buffer()) : 0,
            .static_nodes = statics ? index(state.static_tree.node_buffer()) : 0,
            .moving_count = state.moving,
            .static_count = state.statics,
            .region_instance = state.moving + state.statics + state.triggers,
            .trees = (moving ? 1U : 0U) | (statics ? 2U : 0U)};
}

auto
GpuConstraintSolver::moving_tree() const noexcept -> const GpuBvh&
{
    return m_state->moving_tree;
}

auto
GpuConstraintSolver::static_tree() const noexcept -> const GpuBvh&
{
    return m_state->static_tree;
}

auto
GpuConstraintSolver::trigger_capacity() const noexcept -> std::uint32_t
{
    return m_state->triggers > 0 ? m_state->trigger_pair_capacity : 0;
}

auto
GpuConstraintSolver::trigger_results_buffer() const noexcept -> const Buffer&
{
    return m_state->trigger_results.buffer();
}

auto
GpuConstraintSolver::reports_buffer() const noexcept -> const Buffer&
{
    return m_state->reports.buffer();
}

auto
GpuConstraintSolver::counts_buffer() const noexcept -> const Buffer&
{
    return m_state->counts[m_state->parity].buffer();
}

auto
GpuConstraintSolver::joint_results_buffer() const noexcept -> const Buffer&
{
    return m_state->joint_results.buffer();
}

auto
GpuConstraintSolver::overflowed(const GpuSolverCounts& counts) -> bool
{
    // Room for what was counted, with headroom; colors the passes were not recorded for; islands not converged.
    State& state = *m_state;
    bool grew = false;
    const auto room = [&grew](std::uint32_t counted, std::uint32_t& capacity) {
        if (counted > capacity)
        {
            capacity = with_headroom(counted);
            grew = true;
        }
    };
    room(counts.within_pairs, state.within_capacity);
    room(counts.between_pairs, state.between_capacity);
    room(counts.candidates, state.pair_capacity);
    room(counts.manifold_slots, state.manifold_capacity);
    if (state.triggers > 0)
    {
        room(counts.trigger_moving_pairs, state.trigger_within_capacity);
        room(counts.trigger_static_pairs, state.trigger_between_capacity);
        room(counts.trigger_candidates, state.trigger_pair_capacity);
        room(counts.trigger_slots, state.trigger_manifold_capacity);
    }
    if (state.coloring() && has_all(state.settings.optimizations, GpuOptimization::RECORDED_PASSES_BY_USE))
    {
        if (counts.joint_colors > state.recorded_joint_colors || counts.contact_colors > state.recorded_contact_colors ||
            (counts.overflow_constraints > 0 && !state.overflow_passes))
        {
            state.recorded_joint_colors = std::max(state.recorded_joint_colors, counts.joint_colors);
            state.recorded_contact_colors = std::max(state.recorded_contact_colors, counts.contact_colors);
            state.overflow_passes = state.overflow_passes || counts.overflow_constraints > 0;
            grew = true;
        }
    }
    else if (state.coloring() && counts.colors > state.recorded_colors)
    {
        state.recorded_colors = counts.colors;
        grew = true;
    }
    if (counts.island_changes > 0 && state.island_rounds < 64)
    {
        state.island_rounds *= 2;
        grew = true;
    }
    return grew;
}

void
GpuConstraintSolver::complete_step(const GpuSolverCounts& counts)
{
    // This step's manifolds and contacts are the next one's previous; colors recorded for what this step used (and two
    // more: a pile growing by a color does not replay).
    State& state = *m_state;
    state.parity = 1 - state.parity;
    state.previous_capacity = state.manifold_capacity;
    state.static_dirty = false;
    state.recorded_colors = std::clamp(counts.colors + 2, GPU_XPBD_INITIAL_COLORS, MAXIMUM_GPU_XPBD_COLORS);
    state.recorded_joint_colors = std::min(counts.joint_colors + 2, MAXIMUM_GPU_XPBD_COLORS);
    state.recorded_contact_colors = std::min(counts.contact_colors + 2, MAXIMUM_GPU_XPBD_COLORS);
    state.overflow_passes = counts.overflow_constraints > 0;
    if (state.refitting)
    {
        ++state.tree_age;
    }
    else
    {
        state.tree_age = 1;
        state.tree_refittable = state.moving > 0;
    }
    state.previous_overflow = counts.overflow_constraints;
}

} // namespace gfx::physics::gpu_xpbd
