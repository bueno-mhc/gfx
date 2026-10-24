/// @file camera.cppm
/// @brief `import gfx.camera;`: the camera interface (projections, clip conventions, constexpr matrix functions,
///        Camera, CameraController), the factory that creates the library's cameras and controllers, and the
///        geometric primitives cameras use (frustums, rays, boxes, spheres). It depends on gfx::core and the physics
///        foundations (gfx::physics_common) only — no physics world, no graphics, platform or UI.
export module gfx.camera;

export import gfx.camera.interface;
export import gfx.camera.factory;
