/// @file gpu_bvh.cppm
/// @brief The GPU backend's bounding volume hierarchy (exported by gfx.physics.gpu_xpbd): a binary radix tree built on the
///        GPU from boxes (Karras 2012), optionally restructured by treelets (Karras and Aila 2013) and collapsed into
///        nodes of four children, refit to moved boxes, traversed for overlapping pairs (within a tree, or between two)
///        and for the nearest box along rays — recorded into the caller's command buffer like GpuPrimitives.
///
/// **Build.** The box around every box (a reduction), each box center's 30-bit Morton code in it, a stable sort of the
/// codes (GpuPrimitives), the hierarchy (one invocation per internal node), the bounds bottom up (one invocation per leaf,
/// climbing: the second to reach a node writes it), then the treelet rounds and the four-wide nodes. Every step is exact
/// or ordered (integer keys, a stable sort, unions of boxes, a fixed evaluation order of the costs), so the same boxes give
/// the same tree, bit for bit, whatever the GPU's scheduling and the primitives' variant.
///
/// **Layout** (read back by tests and debug views): GpuBvhHeader; 2n − 1 GpuBvhNode, the internal nodes 0 .. n − 2 (node 0
/// is the root when n ≥ 2), then the leaves in the sorted order; a child is an internal node's index, or
/// GPU_XPBD_BVH_LEAF_FLAG | the leaf's position (its node: n − 1 + position); GpuBvhAnnotation per node; GpuWideBvhNode per
/// internal node for the four-wide layout (those rooting one: the root and the internal children of others).
export module gfx.physics.gpu_xpbd:bvh;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.spatial;
import :settings;
import :primitives;

export namespace gfx::physics::gpu_xpbd
{

/// @brief How a GpuBvh is built and traversed.
struct GpuBvhDescription
{
    GpuBvhLayout layout = GpuBvhLayout::BINARY; ///< FOUR_WIDE: the four-wide nodes are built too and traversed instead.
    std::uint32_t treelet_rounds = 0;           ///< Treelet restructuring rounds after each build (0: none); round r
                                                ///< restructures the nodes over at least GPU_XPBD_TREELET_LEAVES · 2^r leaves.
    std::string_view debug_name = "GPU BVH";    ///< Names its buffers (copied).
};

/// @brief The header of a GpuBvh.
struct GpuBvhHeader
{
    std::uint32_t leaf_count = 0;          ///< Leaves (boxes) of the last build.
    std::uint32_t root = 0;                ///< The root: node 0 when there are two leaves or more, else leaf 0.
    GpuBvhFlag flags = GpuBvhFlag::NONE;   ///< What its traversals reported since the last build.
    std::uint32_t reserved = 0;            ///< (Alignment.)
};

/// @brief A node of a GpuBvh: its box, and its children (an internal node) or its primitive (a leaf: `first`).
struct GpuBvhNode
{
    std::array<float, 3> minimum{};
    std::uint32_t first = 0;               ///< The first child; a leaf's primitive (the index of its box).
    std::array<float, 3> maximum{};
    std::uint32_t second = 0;              ///< The second child; 0 for a leaf.
};

/// @brief What a GpuBvh knows of a node's subtree.
struct GpuBvhAnnotation
{
    std::uint32_t leaf_count = 0;          ///< Leaves under it.
    std::uint32_t last_leaf = 0;           ///< The last leaf position under it.
    float cost = 0;                        ///< Its surface area heuristic cost (GPU_XPBD_BVH_INTERNAL_COST and _LEAF_COST
                                           ///< per unit of area of its nodes).
    std::uint32_t reserved = 0;            ///< (Alignment.)
};

/// @brief A node of a GpuBvh's four-wide layout: four children's boxes by axis, the children (an internal node's index —
///        it roots the four-wide node of that index —, a leaf with GPU_XPBD_BVH_LEAF_FLAG, or INVALID_BINDLESS_INDEX's
///        value 0xFFFFFFFF for an empty slot, whose box is empty), and their last leaf positions.
struct GpuWideBvhNode
{
    std::array<std::array<float, 4>, 3> minimums{};
    std::array<std::array<float, 4>, 3> maximums{};
    std::array<std::uint32_t, 4> children{};
    std::array<std::uint32_t, 4> last_leaves{};
};

/// @brief A ray of GpuBvh::cast_rays().
struct GpuRay
{
    std::array<float, 3> origin{};
    float maximum_distance = 0;            ///< Along the direction (in multiples of its length).
    std::array<float, 3> direction{};
    float reserved = 0;                    ///< (Alignment.)
};

/// @brief What a ray of GpuBvh::cast_rays() entered first.
struct GpuRayHit
{
    std::uint32_t primitive = 0;           ///< The box entered first (the smaller one on ties), or 0xFFFFFFFF.
    float distance = 0;                    ///< Where the ray enters it (0 when it starts inside); the maximum distance
                                           ///< without a hit.
};

/// @brief A pair of overlapping boxes found by GpuBvh::find_pairs().
struct GpuPair
{
    std::uint32_t first = 0;               ///< The query tree's primitive (within a tree: the smaller).
    std::uint32_t second = 0;              ///< The other tree's primitive (within a tree: the larger).
};

/// @brief A bounding volume hierarchy of boxes on the GPU, with its pipelines and its buffers.
class GpuBvh
{
public:
    /// @param device      The device (it outlives the tree).
    /// @param primitives  The primitives it sorts and scans with (they outlive the tree; reserved by reserve()).
    /// @param description Its layout and treelet rounds.
    GpuBvh(Device& device,
           GpuPrimitives& primitives,
           const GpuBvhDescription& description = {});
    ~GpuBvh();

    GpuBvh(const GpuBvh&) = delete;
    auto operator=(const GpuBvh&) -> GpuBvh& = delete;

    /// @brief Makes room for trees of up to @p maximum leaves (and the primitives for them). Call it before recording, as
    ///        GpuPrimitives::reserve().
    /// @throws InvalidUsageError if @p maximum is above GPU_XPBD_MAXIMUM_ELEMENTS.
    void reserve(std::uint32_t maximum);

    /// @brief The largest tree reserved.
    [[nodiscard]] auto reserved() const noexcept -> std::uint32_t;

    /// @brief Its layout and treelet rounds.
    [[nodiscard]] auto description() const noexcept -> const GpuBvhDescription&;

    /// @brief Records the build of a tree over @p count boxes of @p boxes (two vectors each: minimum, maximum; w ignored),
    ///        whose leaves are the boxes' indices. The boxes are not read afterwards.
    /// @throws InvalidUsageError if the count's maximum is above reserved().
    void build(ComputeRecorder& recorder,
               GpuSpan boxes,
               const GpuCount& count);

    /// @brief Records a refit: the same leaves, in the same order, given the boxes of @p boxes (moved boxes of the same
    ///        primitives); the boxes of the nodes, their annotations and the four-wide nodes follow.
    /// @throws InvalidUsageError if nothing was built yet.
    void refit(ComputeRecorder& recorder,
               GpuSpan boxes);

    /// @brief Records the search for the pairs of overlapping boxes of the tree (each pair once, the smaller primitive
    ///        first): their number into the word @p pair_count, the first @p capacity of them into @p pairs (GpuPair), in
    ///        an order that depends only on the tree. More pairs than the capacity: the count says how many.
    /// @throws InvalidUsageError if nothing was built yet.
    void find_pairs(ComputeRecorder& recorder,
                    GpuSpan pairs,
                    GpuSpan pair_count,
                    std::uint32_t capacity);

    /// @brief Records the search for the pairs of a box of this tree and an overlapping box of @p other (this tree's
    ///        primitive first), as the other overload.
    /// @throws InvalidUsageError if either tree was not built yet.
    void find_pairs(ComputeRecorder& recorder,
                    const GpuBvh& other,
                    GpuSpan pairs,
                    GpuSpan pair_count,
                    std::uint32_t capacity);

    /// @brief Records the search, for @p count rays of @p rays (GpuRay), of the box each enters first, into @p hits
    ///        (GpuRayHit).
    /// @throws InvalidUsageError if nothing was built yet.
    void cast_rays(ComputeRecorder& recorder,
                   GpuSpan rays,
                   GpuSpan hits,
                   const GpuCount& count);

    /// @brief The buffer holding the GpuBvhHeader (readable by copies).
    [[nodiscard]] auto header_buffer() const noexcept -> const Buffer&;

    /// @brief The buffer holding the 2n − 1 GpuBvhNode of the last build (readable by copies).
    [[nodiscard]] auto node_buffer() const noexcept -> const Buffer&;

    /// @brief The buffer holding a GpuBvhAnnotation per node (readable by copies).
    [[nodiscard]] auto annotation_buffer() const noexcept -> const Buffer&;

    /// @brief The buffer holding a GpuWideBvhNode per internal node (FOUR_WIDE layout; nullptr otherwise).
    [[nodiscard]] auto wide_node_buffer() const noexcept -> const Buffer*;

    /// @brief The buffer holding the sorted Morton codes of the last build, one word per leaf (readable by copies).
    [[nodiscard]] auto code_buffer() const noexcept -> const Buffer&;

    /// @brief What only the implementation touches (pipelines, buffers).
    struct State;

private:
    std::unique_ptr<State> m_state{};
};

/// @brief Appends the nodes of a tree read back (its header and its 2n − 1 nodes) to @p output, as spatial structures
///        report theirs (BroadPhase::structure_nodes()): depth first from the root, the first child first; depth 0 at
///        the root; one proxy per leaf, none per internal node.
void structure_nodes(const GpuBvhHeader& header,
                     std::span<const GpuBvhNode> nodes,
                     std::vector<SpatialNode>& output);

} // namespace gfx::physics::gpu_xpbd
