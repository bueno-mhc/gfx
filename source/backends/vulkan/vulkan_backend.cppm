/// @file vulkan_backend.cppm
/// @brief Module gfx.backends.vulkan: the Vulkan backend. Only the backend factory imports it; native
///        Vulkan types never leave the backend (its partitions include the Vulkan headers in their
///        global module fragments).
export module gfx.backends.vulkan;

import std;
import gfx.interface;

export namespace gfx::vulkan
{

/// @brief Why the Vulkan backend cannot run on this machine (the Vulkan loader cannot be loaded), or
///        an empty string when it can.
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief Creates a Vulkan instance.
/// @throws BackendApiError when the driver refuses the instance (e.g. no Vulkan 1.3 driver).
[[nodiscard]] auto create_instance(const InstanceDescription& description) -> std::unique_ptr<Instance>;

} // namespace gfx::vulkan
