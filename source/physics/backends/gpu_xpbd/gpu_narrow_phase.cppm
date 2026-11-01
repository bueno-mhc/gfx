/// @file gpu_narrow_phase.cppm
/// @brief The GPU backend's narrow phase (exported by gfx.physics.gpu_xpbd): a table of shapes on the GPU (every kind:
///        convex shapes, triangle meshes, height fields, compounds), the boxes of posed instances (what a GpuBvh is built
///        over), the contact manifolds of each candidate pair, and the contact cache matching this step's points to the
///        previous step's — recorded into the caller's command buffer like GpuPrimitives and GpuBvh.
///
/// **The CPU's narrow phase, on the GPU.** The shaders port physics::collide() operation by operation. A pair becomes its
/// sub-pairs in the order collide() visits them — compounds descended through the CPU's own static hierarchies (the first
/// shape's before the second's), a convex shape against the triangles under its box, two meshes or height fields triangle
/// by triangle — then each sub-pair collides as collide_convex() dispatches it (the analytic pairs, separating axes for two
/// boxes and two triangles, GJK, EPA and face clipping otherwise), triangle contacts one-sided unless the mesh is
/// two-sided and corrected on inactive edges, and each pair's manifolds merge as collide() merges them (normals within the
/// merge cosine, four points, the deepest `maximum_manifolds`). So a pair's manifolds are the ones physics::collide()
/// makes at the same poses, within float rounding (GPU divisions, square roots and sines are not the CPU's).
///
/// **Deterministic.** Sub-pairs and manifolds go to slots scanned from counts; the cache sorts by pair with stable sorts and
/// finds predecessors by binary search. The same inputs give the same bits.
export module gfx.physics.gpu_xpbd:narrow_phase;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.narrow_phase;
import gfx.physics.shapes;
import :settings;
import :primitives;

export namespace gfx::physics::gpu_xpbd
{

/// @brief A shape at a pose, as the narrow phase reads it.
struct GpuInstance
{
    std::array<float, 3> position{};                          ///< The shape's frame in the world.
    std::uint32_t shape = 0;                                  ///< Its index in the GpuNarrowPhase's shape table.
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F}; ///< Unit quaternion x, y, z, w.
};

/// @brief A contact point of a GpuManifold.
struct GpuContactPoint
{
    std::array<float, 3> position{}; ///< Midway between the surfaces (world space).
    float separation = 0.0F;         ///< Along the manifold's normal (negative: overlapping).
    ContactFeature feature{};        ///< As physics::collide() names it (its triangles, its local features).
    std::uint32_t reserved = 0;      ///< (Alignment.)
};

/// @brief A manifold of a pair: up to four points sharing a normal from the first instance to the second.
struct GpuManifold
{
    std::uint32_t first = 0;                   ///< The pair's first instance.
    std::uint32_t second = 0;                  ///< Its second instance.
    std::uint32_t point_count = 0;             ///< 0: an empty slot.
    std::uint32_t algorithm = 0;               ///< The ContactAlgorithm that made it (its value).
    std::uint32_t sub_shape_first = NO_SUB_SHAPE;  ///< The first shape's compound child or triangle it comes from.
    std::uint32_t sub_shape_second = NO_SUB_SHAPE; ///< The second shape's.
    std::array<std::uint32_t, 2> reserved{};   ///< (Alignment.)
    std::array<float, 3> normal{};             ///< Unit length (zero in an empty slot).
    float reserved_normal = 0.0F;              ///< (Alignment.)
    std::array<GpuContactPoint, MAXIMUM_MANIFOLD_POINTS> points{}; ///< The first point_count in use, the others zero.
};

/// @brief Where GpuNarrowPhase::collide() writes.
struct GpuManifoldSpans
{
    GpuSpan manifolds{};        ///< GpuManifold slots: each pair's manifolds from its first slot, the slots after them empty.
    GpuSpan ranges{};           ///< Per pair: its first slot (from `manifolds`) and its manifolds (two words).
    GpuSpan total{};            ///< A word: the slots the pairs needed (above `capacity`: the pairs past it were cut short).
    std::uint32_t capacity = 0; ///< Slots of `manifolds`, at most reserved_manifolds().
};

/// @brief @p manifold as the CPU's narrow phase holds one (its points' impulses zero).
[[nodiscard]] auto to_contact_manifold(const GpuManifold& manifold) noexcept -> ContactManifold;

/// @brief The narrow phase: its pipelines, its shape table, its scratch.
class GpuNarrowPhase
{
public:
    /// @param device     The device (it outlives the narrow phase).
    /// @param primitives The primitives it scans and sorts with (they outlive it; reserved by reserve()).
    GpuNarrowPhase(Device& device,
                   GpuPrimitives& primitives);
    ~GpuNarrowPhase();

    GpuNarrowPhase(const GpuNarrowPhase&) = delete;
    auto operator=(const GpuNarrowPhase&) -> GpuNarrowPhase& = delete;

    /// @brief Adds @p shape to the shape table (a copy: hulls with their faces, meshes with their triangles, active edges
    ///        and static hierarchy, height fields with their heights and active edges, compounds with their children —
    ///        each child added too, a child shared by several stored once).
    /// @return Its index (GpuInstance::shape).
    /// @throws BackendCapabilityError for compounds holding compounds deeper than GPU_XPBD_COMPOUND_DEPTH.
    auto add_shape(const Shape& shape) -> std::uint32_t;

    /// @brief Shapes in the table (a compound's children count).
    [[nodiscard]] auto shape_count() const noexcept -> std::uint32_t;

    /// @brief Records the upload of the shape table (after shapes were added; before recording the commands that read
    ///        it, never between recording and submitting them, as GpuPrimitives::reserve()).
    void upload_shapes(ComputeRecorder& recorder);

    /// @brief Makes room for up to @p maximum_pairs candidate pairs per collide() and @p maximum_manifolds manifold slots
    ///        (collide()'s capacity, the cache's manifolds), and the primitives for them.
    /// @throws InvalidUsageError if either is above GPU_XPBD_MAXIMUM_ELEMENTS.
    void reserve(std::uint32_t maximum_pairs,
                 std::uint32_t maximum_manifolds);

    /// @brief The most candidate pairs reserved.
    [[nodiscard]] auto reserved_pairs() const noexcept -> std::uint32_t;

    /// @brief The most manifold slots reserved.
    [[nodiscard]] auto reserved_manifolds() const noexcept -> std::uint32_t;

    /// @brief Records the box around each of @p count instances of @p instances (GpuInstance; boxes of two vectors into
    ///        @p boxes, as GpuBvh::build() reads them), as Shape::bounds(pose) gives it, grown by @p expansion.
    /// @throws InvalidUsageError if shapes were added since the last upload_shapes(), or @p expansion is negative or not
    ///         finite.
    void bounds(ComputeRecorder& recorder,
                GpuSpan instances,
                GpuSpan boxes,
                const GpuCount& count,
                float expansion = 0.0F);

    /// @brief Records the manifolds of each of @p count pairs of @p pairs (GpuPair: instance indices into @p instances):
    ///        the ones physics::collide() makes with @p settings, into @p output (each pair's range of slots).
    /// @throws InvalidUsageError if shapes were added since the last upload_shapes(), the count's maximum is above
    ///         reserved_pairs(), the capacity above reserved_manifolds(), or @p settings are invalid (as collide() checks
    ///         them).
    void collide(ComputeRecorder& recorder,
                 GpuSpan instances,
                 GpuSpan pairs,
                 const GpuCount& count,
                 const GpuManifoldSpans& output,
                 const CollisionSettings& settings = {});

    /// @brief Records the contact cache: for each point of the @p count manifold slots of @p current, its predecessor
    ///        among the @p previous_count slots of @p previous — the point with the same feature among the previous
    ///        manifolds of the same pair, the first of them — as that manifold's slot × 4 + its point, or 0xFFFFFFFF; four
    ///        words per current slot into @p predecessors (empty slots have none).
    /// @throws InvalidUsageError if the previous count's maximum is above reserved_manifolds().
    void match(ComputeRecorder& recorder,
               GpuSpan previous,
               const GpuCount& previous_count,
               GpuSpan current,
               const GpuCount& count,
               GpuSpan predecessors);

    /// @brief What only the implementation touches (pipelines, tables, scratch).
    struct State;

private:
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::physics::gpu_xpbd
