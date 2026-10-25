/// @file physics.cppm
/// @brief `import gfx.physics;`: the physics library — the interface (worlds, bodies, colliders, joints, triggers, soft
///        bodies, queries, events, pipeline components) with the shapes, the narrow phase, the spatial structures and the
///        task schedulers it re-exports, the framework backends are built on (gfx.physics.world), the factory
///        (create_physics_world()), and what renderers need to show a world (gfx.physics.debug_draw: shapes as
///        triangle meshes, colored lines). The backends themselves are private.
export module gfx.physics;

export import gfx.physics.interface;
export import gfx.physics.world;
export import gfx.physics.factory;
export import gfx.physics.debug_draw;
