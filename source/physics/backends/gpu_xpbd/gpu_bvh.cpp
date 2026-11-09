// GpuBvh: the passes of a build, a refit, the pair searches and the ray casts recorded with their push constants (the
// layouts of the shaders' Push blocks), the tree's buffers sized by reserve(); structure_nodes() on the CPU. A GPU scope
// per build, refit, pair search and ray cast and per pass inside them (PROFILE_GPU_SCOPE through set_timer()'s timer).
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
import gfx.physics.primitives;
import gfx.physics.spatial;
import gfx.physics.gpu_xpbd.shaders;
import :settings;
import :resources;
import :primitives;
import :bvh;

namespace gfx::physics::gpu_xpbd
{

namespace
{

static_assert(GPU_XPBD_MORTON_BITS == 10, "bvh_morton.comp spreads ten bits per axis");
static_assert(3 * GPU_XPBD_MORTON_BITS < 32 && GPU_XPBD_MAXIMUM_ELEMENTS < GPU_XPBD_BVH_LEAF_FLAG);
static_assert(GPU_XPBD_TREELET_LEAVES >= 3 && GPU_XPBD_TREELET_LEAVES <= 8, "the treelet pass weighs 2^leaves subsets per treelet");
static_assert(sizeof(GpuBvhHeader) == 16 && sizeof(GpuBvhNode) == 32 && sizeof(GpuBvhAnnotation) == 16 && sizeof(GpuWideBvhNode) == 128);
static_assert(sizeof(GpuRay) == 32 && sizeof(GpuRayHit) == 8 && sizeof(GpuPair) == 8);

// The push constants of the passes (the Push blocks of the shaders, word by word; Tree in bvh.glsl).
struct TreePush
{
    std::uint32_t header = 0;
    std::uint32_t nodes = 0;
    std::uint32_t parents = 0;
    std::uint32_t annotations = 0;
    std::uint32_t counters = 0;
    std::uint32_t wide_nodes = 0;
};

struct MortonPush
{
    std::uint32_t boxes_buffer = 0;
    std::uint32_t boxes_first = 0;
    std::uint32_t bounds_buffer = 0;
    std::uint32_t keys_buffer = 0;
    std::uint32_t values_buffer = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};

struct HierarchyPush
{
    std::uint32_t codes_buffer = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
    TreePush tree{};
};

struct BoundsPush
{
    std::uint32_t boxes_buffer = 0;
    std::uint32_t boxes_first = 0;
    std::uint32_t primitives_buffer = 0;
    TreePush tree{};
};

struct TreeletPush
{
    TreePush tree{};
    std::uint32_t minimum_leaves = 0;
};

struct PairsPush
{
    std::uint32_t query_header = 0;
    std::uint32_t query_nodes = 0;
    TreePush target{};
    std::uint32_t flags = 0;
    std::uint32_t counts_buffer = 0;
    std::uint32_t offsets_buffer = 0;
    std::uint32_t pairs_buffer = 0;
    std::uint32_t pairs_first = 0;
    std::uint32_t capacity = 0;
};

struct RaysPush
{
    TreePush tree{};
    std::uint32_t flags = 0;
    std::uint32_t rays_buffer = 0;
    std::uint32_t rays_first = 0;
    std::uint32_t hits_buffer = 0;
    std::uint32_t hits_first = 0;
    std::uint32_t maximum = 0;
    std::uint32_t count_buffer = INVALID_BINDLESS_INDEX;
    std::uint32_t count_word = 0;
};
static_assert(sizeof(PairsPush) == 14 * sizeof(std::uint32_t) && sizeof(RaysPush) == 14 * sizeof(std::uint32_t));

// The flags of the pair and ray passes (bvh_pairs.comp, bvh_rays.comp).
constexpr std::uint32_t SAME_TREE = 1;
constexpr std::uint32_t WIDE = 2;
constexpr std::uint32_t WRITE = 4;

/// Throws the InvalidUsageError of @p operation.
[[noreturn]] void
refuse(std::string_view operation,
       std::string problem,
       std::string_view suggestion)
{
    throw InvalidUsageError({.backend = PhysicsBackendKind::GPU_XPBD,
                             .operation = std::format("GpuBvh::{}", operation),
                             .problem = std::move(problem),
                             .suggestion = std::string{suggestion}});
}

/// The node of @p child in a tree of @p leaf_count leaves.
[[nodiscard]] constexpr auto
node_of(std::uint32_t child,
        std::uint32_t leaf_count) noexcept -> std::uint32_t
{
    return (child & GPU_XPBD_BVH_LEAF_FLAG) != 0 ? leaf_count - 1 + (child & ~GPU_XPBD_BVH_LEAF_FLAG) : child;
}

} // namespace

// ===================================================================================================== State

struct GpuBvh::State
{
    State(Device& target,
          GpuPrimitives& shared_primitives,
          const GpuBvhDescription& settings)
        : device{target}
        , primitives{shared_primitives}
        , description{settings}
        , name{settings.debug_name}
        , morton{make_pipeline(target, shaders::bvh_morton_comp, modules)}
        , hierarchy{make_pipeline(target, shaders::bvh_hierarchy_comp, modules)}
        , bounds{make_pipeline(target, shaders::bvh_bounds_comp, modules)}
        , reset{make_pipeline(target, shaders::bvh_reset_comp, modules)}
        , treelet{settings.treelet_rounds > 0 ? make_pipeline(target, shaders::bvh_treelet_comp, modules) : nullptr}
        , wide{settings.layout == GpuBvhLayout::FOUR_WIDE ? make_pipeline(target, shaders::bvh_wide_comp, modules) : nullptr}
        , pairs{make_pipeline(target, shaders::bvh_pairs_comp, modules)}
        , rays{make_pipeline(target, shaders::bvh_rays_comp, modules)}
        , header{buffer(sizeof(GpuBvhHeader), "header")}
        , scene_box{buffer(2 * sizeof(GpuVector), "scene box")}
    {
        description.debug_name = name;
    }

    /// A device-local storage buffer of @p bytes (at least a vector), readable by copies.
    [[nodiscard]] auto buffer(std::uint64_t bytes,
                              std::string_view part) const -> std::unique_ptr<Buffer>
    {
        return device.create_buffer({.size = std::max<std::uint64_t>(bytes, 16),
                                     .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE,
                                     .debug_name = std::format("{}: {}", name, part)});
    }

    /// The tree's buffers, as the passes name them.
    [[nodiscard]] auto tree() const noexcept -> TreePush
    {
        return {.header = header->bindless_index().value(),
                .nodes = nodes->bindless_index().value(),
                .parents = parents->bindless_index().value(),
                .annotations = annotations->bindless_index().value(),
                .counters = counters->bindless_index().value(),
                .wide_nodes = wide_nodes != nullptr ? wide_nodes->bindless_index().value() : INVALID_BINDLESS_INDEX};
    }

    /// The count of the last build's leaves (its header word), at most the build's maximum.
    [[nodiscard]] auto leaf_count() const noexcept -> GpuCount
    {
        return {.maximum = built, .word = span_of(*header, 0)};
    }

    /// Throws when nothing was built.
    void require_built(std::string_view operation) const
    {
        if (!has_built)
        {
            refuse(operation, "nothing was built yet", "record GpuBvh::build() first (in this command buffer or an earlier one)");
        }
    }

    /// The bounds of the leaves bottom up (counters zeroed by the hierarchy pass or a reset), then the four-wide nodes.
    void record_bounds(ComputeRecorder& recorder,
                       GpuSpan boxes) const
    {
        PROFILE_GPU_SCOPE("bvh_bounds", recorder, passes);
        record_pass(recorder, *bounds,
                    BoundsPush{.boxes_buffer = boxes.buffer,
                               .boxes_first = boxes.first,
                               .primitives_buffer = sorted_primitives->bindless_index().value(),
                               .tree = tree()},
                    workgroups(std::max(built, 1U)));
    }

    void record_reset(ComputeRecorder& recorder) const
    {
        PROFILE_GPU_SCOPE("bvh_reset", recorder, passes);
        record_pass(recorder, *reset, tree(), workgroups(std::max(built, 1U)));
    }

    void record_wide(ComputeRecorder& recorder) const
    {
        if (wide != nullptr)
        {
            PROFILE_GPU_SCOPE("bvh_wide", recorder, passes);
            record_pass(recorder, *wide, tree(), workgroups(std::max(built, 1U)));
        }
    }

    /// Records the pair search of this tree's leaves in @p target (this tree itself: @p same_tree).
    void record_pairs(ComputeRecorder& recorder,
                      const State& target,
                      bool same_tree,
                      GpuSpan pair_span,
                      GpuSpan pair_count,
                      std::uint32_t capacity) const
    {
        // Count, scan the counts into offsets (their sum into the pair count), write.
        PROFILE_GPU_SCOPE("BVH pairs", recorder, passes);
        PairsPush push{.query_header = header->bindless_index().value(),
                       .query_nodes = nodes->bindless_index().value(),
                       .target = target.tree(),
                       .flags = (same_tree ? SAME_TREE : 0U) | (target.wide != nullptr ? WIDE : 0U),
                       .counts_buffer = pair_counts->bindless_index().value(),
                       .offsets_buffer = pair_offsets->bindless_index().value(),
                       .pairs_buffer = pair_span.buffer,
                       .pairs_first = pair_span.first,
                       .capacity = capacity};
        {
            PROFILE_GPU_SCOPE("bvh_pairs (count)", recorder, passes);
            record_pass(recorder, *pairs, push, workgroups(std::max(built, 1U)));
        }
        primitives.exclusive_scan(recorder, span_of(*pair_counts), span_of(*pair_offsets), leaf_count(), pair_count);
        push.flags |= WRITE;
        PROFILE_GPU_SCOPE("bvh_pairs (write)", recorder, passes);
        record_pass(recorder, *pairs, push, workgroups(std::max(built, 1U)));
    }

    Device& device;
    GpuPrimitives& primitives;
    GpuBvhDescription description{};
    std::string name{};
    std::vector<std::unique_ptr<ShaderModule>> modules{};
    std::unique_ptr<ComputePipeline> morton{};
    std::unique_ptr<ComputePipeline> hierarchy{};
    std::unique_ptr<ComputePipeline> bounds{};
    std::unique_ptr<ComputePipeline> reset{};
    std::unique_ptr<ComputePipeline> treelet{};
    std::unique_ptr<ComputePipeline> wide{};
    std::unique_ptr<ComputePipeline> pairs{};
    std::unique_ptr<ComputePipeline> rays{};

    // The tree: its header and the box around its boxes always; the rest sized for `maximum` leaves.
    std::unique_ptr<Buffer> header{};
    std::unique_ptr<Buffer> scene_box{};
    std::uint32_t maximum = 0;
    std::uint32_t built = 0;       ///< The maximum of the last build (sizes the dispatches over its leaves).
    bool has_built = false;
    std::unique_ptr<Buffer> codes{};             ///< Morton codes, in the boxes' order.
    std::unique_ptr<Buffer> indices{};           ///< The boxes' indices.
    std::unique_ptr<Buffer> sorted_codes{};      ///< The codes sorted.
    std::unique_ptr<Buffer> sorted_primitives{}; ///< The primitive of each leaf position.
    std::unique_ptr<Buffer> nodes{};             ///< 2n − 1 nodes.
    std::unique_ptr<Buffer> parents{};           ///< A word per node.
    std::unique_ptr<Buffer> annotations{};       ///< An annotation per node.
    std::unique_ptr<Buffer> counters{};          ///< A word per internal node.
    std::unique_ptr<Buffer> wide_nodes{};        ///< A four-wide node per internal node (FOUR_WIDE).
    std::unique_ptr<Buffer> pair_counts{};       ///< A word per leaf: its pairs.
    std::unique_ptr<Buffer> pair_offsets{};      ///< A word per leaf: its first pair.

    PassTimer passes{};                          ///< What its passes are timed through.
};

// ===================================================================================================== GpuBvh

GpuBvh::GpuBvh(Device& device,
               GpuPrimitives& primitives,
               const GpuBvhDescription& description)
    : m_state{std::make_unique<State>(device, primitives, description)}
{
    reserve(GPU_XPBD_WORKGROUP_SIZE);
}

GpuBvh::~GpuBvh() = default;

void
GpuBvh::reserve(std::uint32_t maximum)
{
    State& state = *m_state;
    if (maximum <= state.maximum)
    {
        return;
    }
    if (maximum > GPU_XPBD_MAXIMUM_ELEMENTS)
    {
        refuse("reserve", std::format("{} leaves is above GPU_XPBD_MAXIMUM_ELEMENTS ({})", maximum, GPU_XPBD_MAXIMUM_ELEMENTS),
               "split the boxes between several trees");
    }

    // n leaves: 2n − 1 nodes, n − 1 internal ones.
    state.primitives.reserve(maximum);
    const std::uint64_t leaves = maximum;
    const std::uint64_t node_count = 2 * leaves - 1;
    const std::uint64_t word = sizeof(std::uint32_t);
    state.codes = state.buffer(leaves * word, "codes");
    state.indices = state.buffer(leaves * word, "indices");
    state.sorted_codes = state.buffer(leaves * word, "sorted codes");
    state.sorted_primitives = state.buffer(leaves * word, "sorted primitives");
    state.nodes = state.buffer(node_count * sizeof(GpuBvhNode), "nodes");
    state.parents = state.buffer(node_count * word, "parents");
    state.annotations = state.buffer(node_count * sizeof(GpuBvhAnnotation), "annotations");
    state.counters = state.buffer((leaves - 1) * word, "counters");
    if (state.description.layout == GpuBvhLayout::FOUR_WIDE)
    {
        state.wide_nodes = state.buffer((leaves - 1) * sizeof(GpuWideBvhNode), "four-wide nodes");
    }
    state.pair_counts = state.buffer(leaves * word, "pair counts");
    state.pair_offsets = state.buffer(leaves * word, "pair offsets");
    state.maximum = maximum;
}

auto
GpuBvh::reserved() const noexcept -> std::uint32_t
{
    return m_state->maximum;
}

auto
GpuBvh::description() const noexcept -> const GpuBvhDescription&
{
    return m_state->description;
}

void
GpuBvh::set_timer(profiler::GpuTimer* timer) noexcept
{
    m_state->passes.set(timer);
}

void
GpuBvh::build(ComputeRecorder& recorder,
              GpuSpan boxes,
              const GpuCount& count)
{
    State& state = *m_state;
    if (count.maximum > state.maximum)
    {
        refuse("build", std::format("the count's maximum is {}, above the {} leaves reserved", count.maximum, state.maximum),
               "call GpuBvh::reserve() with the largest count before recording");
    }
    state.built = count.maximum;
    state.has_built = true;
    const std::uint32_t invocations = workgroups(std::max(count.maximum, 1U));
    PROFILE_GPU_SCOPE("BVH build", recorder, state.passes);

    // The codes of the box centers in the box around them, sorted with the boxes' indices.
    state.primitives.reduce_bounds(recorder, boxes, span_of(*state.scene_box), count);
    {
        PROFILE_GPU_SCOPE("bvh_morton", recorder, state.passes);
        record_pass(recorder, *state.morton,
                    MortonPush{.boxes_buffer = boxes.buffer,
                               .boxes_first = boxes.first,
                               .bounds_buffer = state.scene_box->bindless_index().value(),
                               .keys_buffer = state.codes->bindless_index().value(),
                               .values_buffer = state.indices->bindless_index().value(),
                               .maximum = count.maximum,
                               .count_buffer = count.word.buffer,
                               .count_word = count.word.first},
                    invocations);
    }
    state.primitives.sort(recorder, span_of(*state.codes), span_of(*state.indices), span_of(*state.sorted_codes), span_of(*state.sorted_primitives),
                          count, 3 * GPU_XPBD_MORTON_BITS);

    // The hierarchy, the bounds, the treelet rounds (γ doubling), the four-wide nodes.
    {
        PROFILE_GPU_SCOPE("bvh_hierarchy", recorder, state.passes);
        record_pass(recorder, *state.hierarchy,
                    HierarchyPush{.codes_buffer = state.sorted_codes->bindless_index().value(),
                                  .maximum = count.maximum,
                                  .count_buffer = count.word.buffer,
                                  .count_word = count.word.first,
                                  .tree = state.tree()},
                    invocations);
    }
    state.record_bounds(recorder, boxes);
    for (std::uint32_t round = 0; round < state.description.treelet_rounds; ++round)
    {
        state.record_reset(recorder);
        PROFILE_GPU_SCOPE("bvh_treelet", recorder, state.passes);
        record_pass(recorder, *state.treelet,
                    TreeletPush{.tree = state.tree(), .minimum_leaves = GPU_XPBD_TREELET_LEAVES << std::min(round, 24U)}, invocations);
    }
    state.record_wide(recorder);
}

void
GpuBvh::refit(ComputeRecorder& recorder,
              GpuSpan boxes)
{
    const State& state = *m_state;
    state.require_built("refit");
    PROFILE_GPU_SCOPE("BVH refit", recorder, state.passes);
    state.record_reset(recorder);
    state.record_bounds(recorder, boxes);
    state.record_wide(recorder);
}

void
GpuBvh::find_pairs(ComputeRecorder& recorder,
                   GpuSpan pairs,
                   GpuSpan pair_count,
                   std::uint32_t capacity)
{
    const State& state = *m_state;
    state.require_built("find_pairs");
    state.record_pairs(recorder, state, true, pairs, pair_count, capacity);
}

void
GpuBvh::find_pairs(ComputeRecorder& recorder,
                   const GpuBvh& other,
                   GpuSpan pairs,
                   GpuSpan pair_count,
                   std::uint32_t capacity)
{
    const State& state = *m_state;
    state.require_built("find_pairs");
    other.m_state->require_built("find_pairs");
    state.record_pairs(recorder, *other.m_state, &other == this, pairs, pair_count, capacity);
}

void
GpuBvh::cast_rays(ComputeRecorder& recorder,
                  GpuSpan rays,
                  GpuSpan hits,
                  const GpuCount& count)
{
    const State& state = *m_state;
    state.require_built("cast_rays");
    PROFILE_GPU_SCOPE("bvh_rays", recorder, state.passes);
    record_pass(recorder, *state.rays,
                RaysPush{.tree = state.tree(),
                         .flags = state.wide != nullptr ? WIDE : 0U,
                         .rays_buffer = rays.buffer,
                         .rays_first = rays.first,
                         .hits_buffer = hits.buffer,
                         .hits_first = hits.first,
                         .maximum = count.maximum,
                         .count_buffer = count.word.buffer,
                         .count_word = count.word.first},
                workgroups(std::max(count.maximum, 1U)));
}

auto
GpuBvh::header_buffer() const noexcept -> const Buffer&
{
    return *m_state->header;
}

auto
GpuBvh::node_buffer() const noexcept -> const Buffer&
{
    return *m_state->nodes;
}

auto
GpuBvh::annotation_buffer() const noexcept -> const Buffer&
{
    return *m_state->annotations;
}

auto
GpuBvh::wide_node_buffer() const noexcept -> const Buffer*
{
    return m_state->wide_nodes.get();
}

auto
GpuBvh::code_buffer() const noexcept -> const Buffer&
{
    return *m_state->sorted_codes;
}

// ===================================================================================================== structure_nodes

void
structure_nodes(const GpuBvhHeader& header,
                std::span<const GpuBvhNode> nodes,
                std::vector<SpatialNode>& output)
{
    // Depth first from the root, the first child first (pushed last).
    const std::uint32_t leaf_count = header.leaf_count;
    if (leaf_count == 0 || nodes.size() < 2 * std::size_t{leaf_count} - 1)
    {
        return;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pending{{header.root, 0}};
    while (!pending.empty())
    {
        const auto [child, depth] = pending.back();
        pending.pop_back();
        const GpuBvhNode& node = nodes[node_of(child, leaf_count)];
        const bool leaf = (child & GPU_XPBD_BVH_LEAF_FLAG) != 0;
        output.push_back({.box = {.minimum = {.x = node.minimum[0], .y = node.minimum[1], .z = node.minimum[2]},
                                  .maximum = {.x = node.maximum[0], .y = node.maximum[1], .z = node.maximum[2]}},
                          .depth = depth,
                          .proxies = leaf ? 1U : 0U});
        if (!leaf)
        {
            pending.emplace_back(node.second, depth + 1);
            pending.emplace_back(node.first, depth + 1);
        }
    }
}

} // namespace gfx::physics::gpu_xpbd
