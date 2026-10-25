/// @file spatial.cppm
/// @brief `import gfx.physics.spatial;`: spatial structures written against the geometric primitives only — the
///        dynamic BVH (:bvh), loose octrees and quadtrees (:loose_tree), the BSP tree (:bsp), the hashed uniform grid
///        (:uniform_grid), sweep and prune (:sweep_and_prune), what they share (:structure: SpatialStructureConcept,
///        proxies, cell maps), any of them chosen at run time (:spatial_structure: SpatialStructure) — and the broad phase
///        on any of them (:broad_phase: SpatialBroadPhase, CollisionFilter).
///        The same structures serve the physics world, culling and game queries.
export module gfx.physics.spatial;

export import gfx.physics.errors;
export import gfx.physics.handles;
export import gfx.physics.primitives;
export import gfx.physics.tasks;
export import :structure;
export import :bvh;
export import :loose_tree;
export import :bsp;
export import :uniform_grid;
export import :sweep_and_prune;
export import :broad_phase;
export import :spatial_structure;

export namespace gfx::physics
{

/// @brief The default broad phase: a dynamic BVH.
using DefaultBroadPhase = SpatialBroadPhase<Bvh>;

/// @brief A broad phase for flat worlds: a quadtree over X and Z.
using FlatWorldBroadPhase = SpatialBroadPhase<Quadtree<AxisPlane::XZ, 8>>;

static_assert(SpatialStructureConcept<Bvh> && SpatialStructureConcept<Octree<>> && SpatialStructureConcept<Quadtree<>> && SpatialStructureConcept<Bsp>
                  && SpatialStructureConcept<UniformGrid<>> && SpatialStructureConcept<SweepAndPrune<>> && SpatialStructureConcept<SweepAndPrune<3>>,
              "every structure satisfies the concept the broad phase takes");

} // namespace gfx::physics
