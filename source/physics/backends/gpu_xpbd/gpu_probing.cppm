// The GPU backend's internal partition of capability probing: what a physical device or a device lacks, which queue the
// steps go to, the error that names every missing capability.
module gfx.physics.gpu_xpbd:probing;

import std;
import gfx.enums;
import gfx.interface;

namespace gfx::physics::gpu_xpbd
{

/// A capability the backend requires and a device lacks.
struct MissingCapability
{
    std::string name{};     ///< What, e.g. "maximum_compute_workgroup_invocations".
    std::string required{}; ///< The value the backend needs.
    std::string reported{}; ///< The value the device reports.
};

/// What @p physical_device lacks to run the backend (on any device made from it).
[[nodiscard]] auto missing_capabilities(const PhysicalDevice& physical_device) -> std::vector<MissingCapability>;

/// What @p device lacks to run the backend (its physical device's limits, its queues, its enabled features, its bindless
/// table).
[[nodiscard]] auto missing_capabilities(Device& device) -> std::vector<MissingCapability>;

/// The queue type of @p device the steps are submitted to: its COMPUTE queues, else its GRAPHICS queues when they
/// compute; nothing when it has neither.
[[nodiscard]] auto step_queue_type(Device& device) -> std::optional<QueueType>;

/// @p missing as indented lines ("\n  - name: required x, reported y").
[[nodiscard]] auto missing_lines(std::span<const MissingCapability> missing) -> std::string;

/// Throws the BackendCapabilityError of @p operation on @p device_name, which lacks @p missing.
[[noreturn]] void throw_missing(std::string_view operation,
                                std::string_view device_name,
                                std::span<const MissingCapability> missing);

} // namespace gfx::physics::gpu_xpbd
