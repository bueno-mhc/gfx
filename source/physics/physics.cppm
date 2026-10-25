/// @file physics.cppm
/// @brief `import gfx.physics;`: the physics library — the interface (worlds, bodies, colliders, joints, triggers, soft
///        bodies, queries, events, pipeline components) with the shapes, the narrow phase, the spatial structures and the
///        task schedulers it re-exports, the framework backends are built on (gfx.physics.world), and the factory
///        (create_physics_world()). The backends themselves are private.
export module gfx.physics;

export import gfx.physics.interface;
export import gfx.physics.world;
export import gfx.physics.factory;
