/// @file narrow_phase.cppm
/// @brief `import gfx.physics.narrow_phase;`: contacts between shapes — contact points, manifolds and their reduction
///        (:contact), GJK (:gjk) and EPA (:epa), face clipping (:clipping), the analytic pairs and the separating axis
///        test (:analytic), the convex dispatch and distances (:convex), any two shapes (:collide), the manifold cache
///        (:manifold_cache) and queries against one shape: ray casts, shape casts, overlaps, closest points (:queries).
///        Re-exports the shapes.
export module gfx.physics.narrow_phase;

export import gfx.physics.handles;
export import gfx.physics.shapes;
export import :contact;
export import :gjk;
export import :epa;
export import :clipping;
export import :analytic;
export import :convex;
export import :collide;
export import :manifold_cache;
export import :queries;
