/// @file interface.cppm
/// @brief `import gfx.physics.interface;`: what every physics backend implements — the values (:descriptions), the base
///        of the objects (:object), colliders, rigid bodies, joints, triggers and soft bodies (:objects), the pipeline
///        components (:pipeline) and the world (:world). Re-exports the shapes, the narrow phase, the spatial
///        structures and the task schedulers the interface speaks of.
export module gfx.physics.interface;

export import gfx.physics.enums;
export import gfx.physics.errors;
export import gfx.physics.handles;
export import gfx.physics.primitives;
export import gfx.physics.shapes;
export import gfx.physics.narrow_phase;
export import gfx.physics.spatial;
export import gfx.physics.tasks;
export import :descriptions;
export import :object;
export import :objects;
export import :pipeline;
export import :world;
