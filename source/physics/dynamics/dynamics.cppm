/// @file dynamics.cppm
/// @brief The abstraction's dynamics components (`import gfx.physics.dynamics;`): integrators, soft-body presets, the
///        XPBD soft-body solver every world built on SteppedWorld runs for backends without soft bodies of their own, and
///        the sequential-impulse rigid-body solver (the reference backend's).
export module gfx.physics.dynamics;

export import :integrators;
export import :presets;
export import :soft_body_solver;
export import :sequential_impulses;
