/// @file gpu_narrow_phase.cppm
/// @brief The GPU backend's narrow phase (exported by gfx.physics.gpu_xpbd): a table of convex shapes on the GPU, the
///        boxes of posed instances (what a GpuBvh is built over), the contact manifold of each candidate pair, and the
///        contact cache matching this step's points to the previous step's — recorded into the caller's command buffer
///        like GpuPrimitives and GpuBvh.
///
/// **The CPU's narrow phase, on the GPU.** The shaders port gfx.physics.narrow_phase operation by operation: the analytic
/// pairs (sphere–sphere, sphere–capsule, capsule–capsule, sphere–box, either order), the separating axis test for two
/// boxes, and GJK on the cores, EPA when they overlap, face clipping and the reduction to four points for every other
/// pair — with the same tolerances, ties and feature ids, so a GPU manifold is the one physics::collide() makes at the
/// same poses, within float rounding (GPU divisions and square roots are not correctly rounded).
///
/// **Deterministic.** Each pair's manifold is written at the pair's index; the cache sorts by pair with stable sorts and
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
    std::array<float, 3> position{};                    ///< The shape's frame in the world.
    std::uint32_t shape = 0;                            ///< Its index in the GpuNarrowPhase's shape table.
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F}; ///< Unit quaternion x, y, z, w.
};

/// @brief A contact point of a GpuManifold.
struct GpuContactPoint
{
    std::array<float, 3> position{}; ///< Midway between the surfaces (world space).
    float separation = 0.0F;         ///< Along the manifold's normal (negative: overlapping).
    ContactFeature feature{};        ///< As physics::collide() names it.
    std::uint32_t reserved = 0;      ///< (Alignment.)
};

/// @brief The manifold of a pair: up to four points sharing a normal from the first instance to the second.
struct GpuManifold
{
    std::uint32_t first = 0;         ///< The pair's first instance.
    std::uint32_t second = 0;        ///< Its second instance.
    std::uint32_t point_count = 0;   ///< 0: no contact.
    std::uint32_t algorithm = 0;     ///< The ContactAlgorithm that made it (its value).
    std::array<float, 3> normal{};   ///< Unit length (zero without points).
    float reserved = 0.0F;           ///< (Alignment.)
    std::array<GpuContactPoint, MAXIMUM_MANIFOLD_POINTS> points{}; ///< The first point_count in use, the others zero.
};

/// @brief @p manifold as the CPU's narrow phase holds one (its points' impulses zero).
[[nodiscard]] auto to_contact_manifold(const GpuManifold& manifold) noexcept -> ContactManifold;

/// @brief The narrow phase: its pipelines, its shape table, its cache's scratch.
class GpuNarrowPhase
{
public:
    /// @param device     The device (it outlives the narrow phase).
    /// @param primitives The primitives its cache sorts with (they outlive it; reserved by reserve()).
    GpuNarrowPhase(Device& device,
                   GpuPrimitives& primitives);
    ~GpuNarrowPhase();

    GpuNarrowPhase(const GpuNarrowPhase&) = delete;
    auto operator=(const GpuNarrowPhase&) -> GpuNarrowPhase& = delete;

    /// @brief Adds @p shape to the shape table (a copy: hulls with their vertices, faces and polygons).
    /// @return Its index (GpuInstance::shape).
    /// @throws BackendCapabilityError for triangle meshes, height fields and compounds (phase G4 of
    ///         docs/gpu-xpbd-design.md brings them).
    auto add_shape(const Shape& shape) -> std::uint32_t;

    /// @brief Shapes in the table.
    [[nodiscard]] auto shape_count() const noexcept -> std::uint32_t;

    /// @brief Records the upload of the shape table (after shapes were added; before recording the commands that read
    ///        it, never between recording and submitting them, as GpuPrimitives::reserve()).
    void upload_shapes(ComputeRecorder& recorder);

    /// @brief Makes room for caches of up to @p maximum manifolds (and the primitives for them).
    /// @throws InvalidUsageError if @p maximum is above GPU_XPBD_MAXIMUM_ELEMENTS.
    void reserve(std::uint32_t maximum);

    /// @brief The largest cache reserved.
    [[nodiscard]] auto reserved() const noexcept -> std::uint32_t;

    /// @brief Records the box around each of @p count instances of @p instances (GpuInstance; boxes of two vectors into
    ///        @p boxes, as GpuBvh::build() reads them): tight from the shapes' support functions, grown by @p expansion.
    /// @throws InvalidUsageError if shapes were added since the last upload_shapes(), or @p expansion is negative or not
    ///         finite.
    void bounds(ComputeRecorder& recorder,
                GpuSpan instances,
                GpuSpan boxes,
                const GpuCount& count,
                float expansion = 0.0F);

    /// @brief Records the manifold of each of @p count pairs of @p pairs (GpuPair: instance indices into @p instances)
    ///        into @p manifolds (GpuManifold, at the pair's index): contacts up to @p settings' speculative distance.
    /// @throws InvalidUsageError if shapes were added since the last upload_shapes(), or the speculative distance is
    ///         negative or not finite.
    void collide(ComputeRecorder& recorder,
                 GpuSpan instances,
                 GpuSpan pairs,
                 GpuSpan manifolds,
                 const GpuCount& count,
                 const CollisionSettings& settings = {});

    /// @brief Records the contact cache: for each point of the @p count manifolds of @p current, its predecessor among
    ///        the @p previous_count manifolds of @p previous — the point of the manifold of the same pair (each pair at
    ///        most once among them) with the same feature — as that manifold's index × 4 + its point, or 0xFFFFFFFF;
    ///        four words per current manifold into @p predecessors.
    /// @throws InvalidUsageError if the previous count's maximum is above reserved().
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
