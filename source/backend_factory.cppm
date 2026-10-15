/// @file backend_factory.cppm
/// @brief Module gfx.backend_factory: the only public entry to the backends. Applications choose a
///        backend by its BackendKind; the backend modules themselves are private to the library.
export module gfx.backend_factory;

import std;
import gfx.enums;
import gfx.interface;

export namespace gfx
{

/// @brief Whether a backend can be used: built into the library, implemented, and its native runtime
///        present on this machine.
struct BackendAvailability
{
    BackendKind backend = BackendKind::NULL_BACKEND; ///< The backend.
    bool built = false;                              ///< Compiled into the library (CMake option GFX_BACKEND_<NAME>).
    bool available = false;                          ///< create_instance() can succeed.
    std::string reason{};                            ///< Why it is unavailable (empty when available).
};

/// @brief Whether @p backend can be used on this machine, and why not.
[[nodiscard]] auto backend_availability(BackendKind backend) -> BackendAvailability;

/// @brief The backends create_instance() can create on this machine, best first (native APIs before
///        the null backend).
[[nodiscard]] auto available_backends() -> std::vector<BackendKind>;

/// @brief Creates the instance of @p backend: the entry object to its physical devices.
/// @throws BackendCapabilityError when the backend was not built, is not implemented, or its native
///         runtime (e.g. the Vulkan loader) is missing; the message says which and what to do.
[[nodiscard]] auto create_instance(BackendKind backend, const InstanceDescription& description = {})
    -> std::unique_ptr<Instance>;

} // namespace gfx
