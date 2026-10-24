/// @file interface.cppm
/// @brief The camera interface (module gfx.camera.interface): projections, clip conventions and the constexpr matrix
///        functions (:projection), the abstract Camera (:camera) and camera controllers (:controller). It re-exports the
///        geometric primitives cameras produce and test (gfx.physics.primitives), the errors cameras throw (gfx.core.errors,
///        gfx.physics.errors) and nothing of the physics interface: a camera needs no physics world.
export module gfx.camera.interface;

export import gfx.core.errors;
export import gfx.core.math;
export import gfx.core.types;
export import gfx.physics.enums;
export import gfx.physics.errors;
export import gfx.physics.primitives;
export import :projection;
export import :camera;
export import :controller;
