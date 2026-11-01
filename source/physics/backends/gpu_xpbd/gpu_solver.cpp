// GpuContactSolver: the contact passes of a step recorded in order, their arrays grown with headroom from what earlier
// steps counted, the colliders and lists uploaded when they change, the previous step's manifolds and contacts kept for
// the contact cache (two of each, swapped when a step completes).
module gfx.physics.gpu_xpbd;

import std;
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

/// The push constants of the contact passes (Push in solver.glsl).
struct SolverPush
{
    std::uint32_t parameters = 0; ///< The step parameters' bindless index.
    std::uint32_t solver = 0;     ///< The solver parameters'.
    std::uint32_t argument = 0;   ///< The pass's: a color, a round, a buffer.
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
};

/// Bytes of the indirect dispatch arguments of a color.
constexpr std::uint32_t DISPATCH_ARGUMENTS_BYTES = 3 * sizeof(std::uint32_t);

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

struct GpuContactSolver::State
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
        , collider_poses{make_pipeline(target, shaders::collider_poses_comp, modules)}
        , pairs_filter{make_pipeline(target, shaders::pairs_filter_comp, modules)}
        , contact_prepare{make_pipeline(target, shaders::contact_prepare_comp, modules)}
        , islands{make_pipeline(target, shaders::islands_comp, modules)}
        , graph{make_pipeline(target, shaders::graph_comp, modules)}
        , ranges_pass{make_pipeline(target, shaders::ranges_comp, modules)}
        , coloring_pass{make_pipeline(target, shaders::color_comp, modules)}
        , contact_solve{make_pipeline(target, shaders::contact_solve_comp, modules)}
        , contact_velocity{make_pipeline(target, shaders::contact_velocity_comp, modules)}
        , jacobi_gather{make_pipeline(target, shaders::jacobi_gather_comp, modules)}
        , sleep{make_pipeline(target, shaders::sleep_comp, modules)}
    {
    }

    [[nodiscard]] auto coloring() const noexcept -> bool
    {
        return settings.solver_mode == GpuSolverMode::GRAPH_COLORING;
    }

    [[nodiscard]] auto candidates() const noexcept -> std::uint32_t
    {
        return within_capacity + between_capacity;
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

    /// A pass over the constraints of @p color, dispatched by the arguments the coloring wrote.
    void color_pass(ComputeRecorder& recorder,
                    const ComputePipeline& pipeline,
                    std::uint32_t color,
                    std::uint32_t mode) const
    {
        recorder.bind_pipeline(pipeline.handle());
        recorder.push_constants(SolverPush{.parameters = step_parameters, .solver = solver_parameters.index(), .argument = color, .mode = mode});
        recorder.dispatch_indirect(color_arguments.buffer().handle(), std::uint64_t{color} * DISPATCH_ARGUMENTS_BYTES);
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
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::unique_ptr<ComputePipeline> collider_poses{};
    std::unique_ptr<ComputePipeline> pairs_filter{};
    std::unique_ptr<ComputePipeline> contact_prepare{};
    std::unique_ptr<ComputePipeline> islands{};
    std::unique_ptr<ComputePipeline> graph{};
    std::unique_ptr<ComputePipeline> ranges_pass{};
    std::unique_ptr<ComputePipeline> coloring_pass{};
    std::unique_ptr<ComputePipeline> contact_solve{};
    std::unique_ptr<ComputePipeline> contact_velocity{};
    std::unique_ptr<ComputePipeline> jacobi_gather{};
    std::unique_ptr<ComputePipeline> sleep{};

    // Colliders and lists (CPU copies uploaded when they change).
    std::vector<GpuCollider> collider_data{};
    std::vector<std::uint32_t> lists{};
    std::uint32_t moving = 0;
    std::uint32_t statics = 0;
    bool colliders_pending = false;
    bool static_dirty = false;
    bool static_built = false;

    // Capacities, recorded passes, the current half of the double buffers.
    std::uint32_t body_count = 0;
    std::uint32_t within_capacity = 0;
    std::uint32_t between_capacity = 0;
    std::uint32_t manifold_capacity = 0;
    std::uint32_t previous_capacity = 0;
    std::uint32_t recorded_colors = GPU_XPBD_INITIAL_COLORS;
    std::uint32_t island_rounds = 8;
    std::uint32_t parity = 0;
    std::uint32_t step_parameters = 0;

    // Buffers.
    HostBuffer solver_parameters{"GPU physics: solver parameters", MemoryLocation::HOST_UPLOAD, BufferUsage::STORAGE};
    HostBuffer upload{"GPU physics: collider upload", MemoryLocation::HOST_UPLOAD, BufferUsage::TRANSFER_SOURCE};
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
};

// ===================================================================================================== GpuContactSolver

GpuContactSolver::GpuContactSolver(Device& device,
                                   const GpuXpbdCapabilities& capabilities,
                                   const GpuXpbdDescription& settings)
    : m_state{std::make_unique<State>(device, capabilities, settings)}
{
}

GpuContactSolver::~GpuContactSolver() = default;

auto
GpuContactSolver::narrow_phase() noexcept -> GpuNarrowPhase&
{
    return m_state->narrow_phase;
}

void
GpuContactSolver::set_colliders(std::span<const GpuCollider> colliders,
                                std::span<const std::uint32_t> moving,
                                std::span<const std::uint32_t> statics,
                                bool static_changed)
{
    State& state = *m_state;
    state.collider_data.assign(colliders.begin(), colliders.end());
    state.lists.assign(moving.begin(), moving.end());
    state.lists.insert(state.lists.end(), statics.begin(), statics.end());
    state.moving = static_cast<std::uint32_t>(moving.size());
    state.statics = static_cast<std::uint32_t>(statics.size());
    state.colliders_pending = true;
    state.static_dirty = state.static_dirty || static_changed;
}

void
GpuContactSolver::record_uploads(ComputeRecorder& recorder,
                                 std::uint32_t body_count,
                                 std::vector<std::unique_ptr<Buffer>>& retired)
{
    // Capacities: what earlier steps counted (overflowed() grows them), at least room for the moving colliders.
    State& state = *m_state;
    state.body_count = body_count;
    state.within_capacity = std::max(state.within_capacity, with_headroom(2 * state.moving));
    state.between_capacity = std::max(state.between_capacity, with_headroom(2 * state.moving));
    state.manifold_capacity = std::max(state.manifold_capacity, with_headroom(state.candidates()));
    const std::uint32_t candidates = state.candidates();
    const std::uint32_t slots = state.manifold_capacity;
    const std::uint32_t instances = state.moving + state.statics;

    // The components' room (before any of their commands is recorded).
    state.primitives.reserve(std::max({candidates, 2 * slots, body_count, instances, 64U}));
    state.narrow_phase.reserve(candidates, slots);
    state.moving_tree.reserve(std::max(state.moving, 1U));
    state.static_tree.reserve(std::max(state.statics, 1U));

    // The arrays (scratch ones need no copy; the previous step's halves keep their contents).
    Device& device = state.device;
    const auto grow = [&](GpuArray& array, std::uint32_t count, std::uint32_t kept = 0) { (void)array.reserve(device, recorder, count, kept, retired); };
    const std::uint32_t current = state.parity;
    const std::uint32_t previous = 1 - state.parity;
    grow(state.colliders, static_cast<std::uint32_t>(state.collider_data.size()));
    grow(state.collider_list, instances);
    grow(state.instances, instances);
    grow(state.boxes, instances);
    for (GpuArray* array : {&state.raw_pairs, &state.pair_flags, &state.pair_indices, &state.pairs, &state.pair_ranges})
    {
        grow(*array, candidates);
    }
    grow(state.manifolds[current], slots);
    grow(state.manifolds[previous], slots, state.previous_capacity);
    grow(state.contacts[current], slots);
    grow(state.contacts[previous], slots, state.previous_capacity);
    grow(state.counts[current], 1);
    grow(state.counts[previous], 1, 1);
    for (GpuArray* array : {&state.predecessors, &state.multipliers, &state.corrections, &state.colors_first, &state.colors_second, &state.color_keys,
                            &state.color_values, &state.color_sorted_keys, &state.color_lists})
    {
        grow(*array, slots);
    }
    for (GpuArray* array : {&state.graph_keys, &state.graph_values, &state.graph_sorted_keys, &state.graph_sorted_values})
    {
        grow(*array, 2 * slots);
    }
    for (GpuArray* array : {&state.body_begin, &state.body_end, &state.labels, &state.island_flags, &state.island_timers})
    {
        grow(*array, body_count);
    }
    grow(state.color_begin, MAXIMUM_GPU_XPBD_COLORS + 2);
    grow(state.color_end, MAXIMUM_GPU_XPBD_COLORS + 2);
    grow(state.color_arguments, MAXIMUM_GPU_XPBD_COLORS + 1);
    state.solver_parameters.reserve(device, sizeof(GpuSolverParameters), retired);

    // What the growth copied and zeroed (here and in the backend's arrays, recorded before) lands before the uploads and
    // fills that write the same arrays.
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_TRANSFER, 1}});

    // The colliders and lists when they changed, the shape table, this step's counts cleared.
    if (state.colliders_pending)
    {
        const std::span<const std::byte> collider_bytes = std::as_bytes(std::span{state.collider_data});
        const std::span<const std::byte> list_bytes = std::as_bytes(std::span{state.lists});
        state.upload.reserve(device, std::max<std::uint64_t>(collider_bytes.size() + list_bytes.size(), 16), retired);
        const std::span<std::byte> mapped = state.upload.bytes();
        std::ranges::copy(collider_bytes, mapped.begin());
        std::ranges::copy(list_bytes, mapped.begin() + static_cast<std::ptrdiff_t>(collider_bytes.size()));
        state.upload.buffer().flush_mapped_range();
        if (!collider_bytes.empty())
        {
            const std::array regions{BufferCopyRegion{.size = collider_bytes.size()}};
            recorder.copy_buffer(state.upload.buffer().handle(), state.colliders.buffer().handle(), regions);
        }
        if (!list_bytes.empty())
        {
            const std::array regions{BufferCopyRegion{.source_offset = collider_bytes.size(), .size = list_bytes.size()}};
            recorder.copy_buffer(state.upload.buffer().handle(), state.collider_list.buffer().handle(), regions);
        }
        state.colliders_pending = false;
    }
    state.narrow_phase.upload_shapes(recorder);
    recorder.fill_buffer(state.counts[current].buffer().handle(), 0, WHOLE_SIZE, 0);
}

void
GpuContactSolver::record_detection(ComputeRecorder& recorder,
                                   const ContactStep& step)
{
    // The parameters.
    State& state = *m_state;
    const std::uint32_t current = state.parity;
    const std::uint32_t previous = 1 - state.parity;
    const std::uint32_t slots = state.manifold_capacity;
    const std::uint32_t candidates = state.candidates();
    const std::uint32_t instances = state.moving + state.statics;
    state.step_parameters = step.step_parameters;
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
    };
    std::memcpy(state.solver_parameters.bytes().data(), &parameters, sizeof(parameters));
    state.solver_parameters.buffer().flush_mapped_range();

    // Instances and boxes; the moving tree, the static tree when its colliders changed; their pairs.
    if (instances > 0)
    {
        state.pass(recorder, *state.collider_poses, 0, 0, instances);
        state.narrow_phase.bounds(recorder, span_of(state.instances.buffer()), span_of(state.boxes.buffer()), {.maximum = instances}, step.speculative_distance);
    }
    if (state.static_dirty)
    {
        state.static_built = false;
        if (state.statics > 0)
        {
            state.static_tree.build(recorder, span_of(state.boxes.buffer(), state.moving), {.maximum = state.statics});
            state.static_built = true;
        }
    }
    if (state.moving > 0)
    {
        state.moving_tree.build(recorder, span_of(state.boxes.buffer()), {.maximum = state.moving});
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

    // The pairs worth a narrow phase, compacted in order; their manifolds; each point's predecessor.
    state.pass(recorder, *state.pairs_filter, 0, 0, candidates);
    state.primitives.compact(recorder, span_of(state.pair_flags.buffer()), {}, span_of(state.pair_indices.buffer()), state.count(CANDIDATES), {.maximum = candidates});
    state.pass(recorder, *state.pairs_filter, 0, 1, candidates);
    CollisionSettings collision = step.collision;
    collision.speculative_distance = step.speculative_distance;
    state.narrow_phase.collide(recorder, span_of(state.instances.buffer()), span_of(state.pairs.buffer()), {.maximum = candidates, .word = state.count(CANDIDATES)},
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

    // The contact constraints.
    state.pass(recorder, *state.contact_prepare, 0, keeps_anchors ? 0U : 1U, slots);

    // Islands: labels hooked and compressed (the last round counts its changes), awake islands, kinematic touches,
    // bodies woken.
    state.pass(recorder, *state.islands, 0, 0, step.body_count);
    for (std::uint32_t round = 0; round < state.island_rounds; ++round)
    {
        state.pass(recorder, *state.islands, round + 1 == state.island_rounds ? 1U : 0U, 1, slots);
        state.pass(recorder, *state.islands, 0, 2, step.body_count);
    }
    state.pass(recorder, *state.islands, 0, 3, step.body_count);
    state.pass(recorder, *state.islands, 0, 4, slots);
    state.pass(recorder, *state.islands, 0, 5, step.body_count);

    // The graph: each body's constraints in slot order.
    state.pass(recorder, *state.graph, 0, 0, slots);
    state.primitives.sort(recorder, span_of(state.graph_keys.buffer()), span_of(state.graph_values.buffer()), span_of(state.graph_sorted_keys.buffer()),
                          span_of(state.graph_sorted_values.buffer()), {.maximum = 2 * slots, .word = state.count(GRAPH_ENTRIES)});
    state.ranges(recorder, state.graph_sorted_keys, state.body_begin, state.body_end, step.body_count, 2 * slots, GRAPH_ENTRIES);

    // The colors (the overflow set only, in the Jacobi mode), the lists by color, their dispatch arguments.
    const bool coloring = state.coloring();
    state.pass(recorder, *state.coloring_pass, coloring ? 0U : 1U, 0, slots);
    const std::uint32_t rounds = coloring ? GPU_XPBD_COLORING_ROUNDS : 0;
    for (std::uint32_t round = 0; round < rounds; ++round)
    {
        state.pass(recorder, *state.coloring_pass, round, 1, slots);
    }
    state.pass(recorder, *state.coloring_pass, rounds % 2, 2, slots);
    state.primitives.sort(recorder, span_of(state.color_keys.buffer()), span_of(state.color_values.buffer()), span_of(state.color_sorted_keys.buffer()),
                          span_of(state.color_lists.buffer()), {.maximum = slots, .word = state.count(SLOTS)}, key_bits(MAXIMUM_GPU_XPBD_COLORS + 1));
    state.ranges(recorder, state.color_sorted_keys, state.color_begin, state.color_end, MAXIMUM_GPU_XPBD_COLORS + 2, slots, SLOTS);
    state.pass(recorder, *state.coloring_pass, state.color_arguments.index(), 3, MAXIMUM_GPU_XPBD_COLORS + 1, COMPUTE_TO_INDIRECT);
}

void
GpuContactSolver::record_positions(ComputeRecorder& recorder,
                                   bool first)
{
    // The colors one after the other (Gauss-Seidel), then the overflow set (Jacobi) and its gather.
    const State& state = *m_state;
    const std::uint32_t reset = first ? 1U : 0U;
    const std::uint32_t colors = state.coloring() ? state.recorded_colors : 0;
    for (std::uint32_t color = 0; color < colors; ++color)
    {
        state.color_pass(recorder, *state.contact_solve, color, reset);
    }
    state.color_pass(recorder, *state.contact_solve, MAXIMUM_GPU_XPBD_COLORS, reset | 2U);
    state.pass(recorder, *state.jacobi_gather, 0, 0, state.body_count);
}

void
GpuContactSolver::record_velocities(ComputeRecorder& recorder,
                                    bool first)
{
    const State& state = *m_state;
    const std::uint32_t reset = first ? 1U : 0U;
    const std::uint32_t colors = state.coloring() ? state.recorded_colors : 0;
    for (std::uint32_t color = 0; color < colors; ++color)
    {
        state.color_pass(recorder, *state.contact_velocity, color, reset);
    }
    state.color_pass(recorder, *state.contact_velocity, MAXIMUM_GPU_XPBD_COLORS, reset | 2U);
    state.pass(recorder, *state.jacobi_gather, 0, 1, state.body_count);
}

void
GpuContactSolver::record_sleep(ComputeRecorder& recorder)
{
    const State& state = *m_state;
    state.pass(recorder, *state.sleep, 0, 0, state.body_count);
    state.pass(recorder, *state.sleep, 0, 1, state.body_count);
}

auto
GpuContactSolver::counts_buffer() const noexcept -> const Buffer&
{
    return m_state->counts[m_state->parity].buffer();
}

auto
GpuContactSolver::overflowed(const GpuSolverCounts& counts) -> bool
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
    room(counts.manifold_slots, state.manifold_capacity);
    if (state.coloring() && counts.colors > state.recorded_colors)
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
GpuContactSolver::complete_step(const GpuSolverCounts& counts)
{
    // This step's manifolds and contacts are the next one's previous; colors recorded for what this step used (and two
    // more: a pile growing by a color does not replay).
    State& state = *m_state;
    state.parity = 1 - state.parity;
    state.previous_capacity = state.manifold_capacity;
    state.static_dirty = false;
    state.recorded_colors = std::clamp(counts.colors + 2, GPU_XPBD_INITIAL_COLORS, MAXIMUM_GPU_XPBD_COLORS);
}

} // namespace gfx::physics::gpu_xpbd
