/// @file sdl3_backend.cppm
/// @brief Module gfx.platform.backends.sdl3: the SDL3 platform backend. Only the platform backend
///        factory imports it; SDL types never leave the backend (its units include the SDL headers in
///        their global module fragments).
export module gfx.platform.backends.sdl3;

import std;
import gfx.platform.interface;

export namespace gfx::platform::sdl3
{

/// @brief Why the SDL3 backend cannot run here, or an empty string when it can (SDL is linked into the
///        library; a failure to start the video subsystem is reported by create_platform()).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief Creates the SDL3 platform on the calling thread (its platform thread).
/// @throws InvalidUsageError when an SDL3 platform exists already (SDL has one event queue per
///         process), BackendApiError when SDL cannot start its video or gamepad subsystem.
[[nodiscard]] auto create_platform(const PlatformDescription& description) -> std::unique_ptr<Platform>;

} // namespace gfx::platform::sdl3
