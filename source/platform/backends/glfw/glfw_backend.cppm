/// @file glfw_backend.cppm
/// @brief Module gfx.platform.backends.glfw: the GLFW platform backend. Only the platform backend
///        factory imports it; GLFW types never leave the backend.
export module gfx.platform.backends.glfw;

import std;
import gfx.platform.interface;

export namespace gfx::platform::glfw
{

/// @brief Why the GLFW backend cannot run here, or an empty string when it can (GLFW is linked into the
///        library; a failure to start is reported by create_platform()).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief Whether a GLFW platform exists now.
[[nodiscard]] auto is_active() noexcept -> bool;

/// @brief Creates the GLFW platform on the calling thread (its platform thread).
/// @throws InvalidUsageError when a GLFW platform exists already (GLFW is initialized once per
///         process), BackendApiError when GLFW cannot start.
[[nodiscard]] auto create_platform(const PlatformDescription& description) -> std::unique_ptr<Platform>;

} // namespace gfx::platform::glfw
