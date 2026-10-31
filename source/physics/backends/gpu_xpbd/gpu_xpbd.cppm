/// @file gpu_xpbd.cppm
/// @brief `import gfx.physics.gpu_xpbd;`: the GPU backend (docs/gpu-xpbd-design.md) — extended position-based dynamics
///        run entirely by compute shaders of the graphics library (storage buffers, compute pipelines, dispatches,
///        barriers; no vendor extension). The factory makes its worlds on a device of their own
///        (create_physics_world(description, PhysicsBackendKind::GPU_XPBD)); this module also makes them on the
///        application's device (shared with its renderer), with the backend's own settings.
///
/// Built with the CMake option GFX_PHYSICS_BACKEND_GPU_XPBD (target gfx::physics_gpu_xpbd), the only part of the physics
/// library that depends on graphics.
///
/// **Phase G0** (foundations): rigid bodies move under gravity, forces, impulses, damping and their motion locks;
/// kinematic bodies by their velocities. Colliders, joints, triggers and soft bodies are refused with a
/// BackendCapabilityError naming the phase that brings them; user constraints are refused for good (their rows are C++
/// code, which the GPU cannot run).
export module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.interface;
export import :settings;
export import :primitives;

export namespace gfx::physics::gpu_xpbd
{

/// @brief What the backend needs of @p device and the variants it would choose there with @p variants.
/// @throws BackendCapabilityError naming every required capability @p device lacks, each with the value required and
///         the value reported (a queue with COMPUTE capability, workgroup sizes, shared memory, storage-buffer range, the
///         shader memory model, bindless storage buffers).
[[nodiscard]] auto capabilities(Device& device,
                                const GpuXpbdVariants& variants = {}) -> GpuXpbdCapabilities;

/// @brief @p capabilities as lines of text (device, queue, limits, chosen variants, optional features).
[[nodiscard]] auto to_string(const GpuXpbdCapabilities& capabilities) -> std::string;

/// @brief Why the factory cannot make a world of the backend here (empty: it can): no Vulkan backend, or no physical
///        device with the required capabilities (each one's missing capabilities listed).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief A world on a device of its own: a Vulkan instance (without validation layers) and a device on the most
///        capable physical device that has what the backend needs, with one compute queue and 64-bit atomics when the
///        physical device has them. What PhysicsBackendKind::GPU_XPBD makes.
/// @throws BackendCapabilityError if no physical device can run the backend (unavailable_reason()).
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description,
                                        const GpuXpbdDescription& settings = {}) -> std::unique_ptr<PhysicsWorld>;

/// @brief A world on @p device (shared with the application: its renderer may read the same GPU). The steps are
///        submitted to its first COMPUTE queue, or its first GRAPHICS queue when it has none. @p device must outlive
///        the world.
/// @throws BackendCapabilityError naming every required capability @p device lacks (capabilities()).
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description,
                                        Device& device,
                                        const GpuXpbdDescription& settings = {}) -> std::unique_ptr<PhysicsWorld>;

} // namespace gfx::physics::gpu_xpbd
