/// @file backend_factory.cppm
/// @brief Module gfx.platform.backend_factory: the only public entry to the platform backends.
///        Applications choose a backend by its PlatformBackendKind; the backend modules themselves are
///        private to the library.
export module gfx.platform.backend_factory;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.interface;

export namespace gfx::platform
{

/// @brief Whether a platform backend can be used: built into the library, implemented, and able to
///        start on this machine.
struct PlatformBackendAvailability
{
    PlatformBackendKind backend = PlatformBackendKind::SDL3; ///< The backend.
    bool built = false;                                      ///< Compiled into the library.
    bool available = false;                                  ///< create_platform() can succeed.
    std::string reason{};                                    ///< Why it is unavailable (empty when available).
};

/// @brief Whether @p backend can be used here, and why not.
[[nodiscard]] auto
platform_backend_availability(PlatformBackendKind backend) -> PlatformBackendAvailability;

/// @brief The backends create_platform() can create here, best first.
[[nodiscard]] auto
available_platform_backends() -> std::vector<PlatformBackendKind>;

/// @brief Creates the platform of @p backend on the calling thread, which becomes its platform thread.
/// @throws BackendCapabilityError when the backend was not built, is not implemented, or cannot start
///         here; the message says which and what to do.
[[nodiscard]] auto
create_platform(PlatformBackendKind backend,
                const PlatformDescription& description = {}) -> std::unique_ptr<Platform>;

} // namespace gfx::platform
