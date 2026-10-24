// The camera factory: the standard implementation, and the reason CUSTOM is never built here.
module gfx.camera.factory;

import std;
import gfx.core.errors;
import gfx.physics.enums;
import gfx.camera.interface;
import gfx.camera.backends.standard;

namespace gfx::camera
{

namespace
{

/// Why the factory cannot build CUSTOM cameras.
constexpr std::string_view CUSTOM_REASON = "an application's camera is a class derived from gfx::camera::Camera (and its controllers from "
                                           "CameraController), constructed by the application; the factory builds the library's cameras only";

/// Throws the BackendCapabilityError of an unavailable backend.
[[noreturn]] void
reject(CameraBackendKind backend,
       std::string_view operation,
       std::string_view capability,
       std::string_view reason)
{
    throw BackendCapabilityError({.backend = backend,
                                  .operation = std::string{operation},
                                  .capability = std::string{capability},
                                  .reason = std::string{reason},
                                  .suggestion = "construct the derived class directly, or use CameraBackendKind::STANDARD"});
}

} // namespace

auto
camera_backend_availability(CameraBackendKind backend) -> CameraBackendAvailability
{
    if (backend == CameraBackendKind::STANDARD)
    {
        std::string reason = standard::unavailable_reason();
        const bool available = reason.empty();
        return {.backend = backend, .built = true, .available = available, .reason = std::move(reason)};
    }
    return {.backend = backend, .built = false, .available = false, .reason = std::string{CUSTOM_REASON}};
}

auto
available_camera_backends() -> std::vector<CameraBackendKind>
{
    std::vector<CameraBackendKind> backends;
    for (const auto& entry : EnumerationTraits<CameraBackendKind>::NAMES)
    {
        if (camera_backend_availability(entry.value).available)
        {
            backends.push_back(entry.value);
        }
    }
    return backends;
}

auto
create_camera(const CameraDescription& description,
              CameraBackendKind backend) -> std::unique_ptr<Camera>
{
    const CameraBackendAvailability availability = camera_backend_availability(backend);
    if (!availability.available)
    {
        reject(backend, "create_camera", "creating cameras through the factory", availability.reason);
    }
    return standard::create_camera(description);
}

auto
create_camera_controller(const CameraControllerDescription& description,
                         CameraBackendKind backend) -> std::unique_ptr<CameraController>
{
    const CameraBackendAvailability availability = camera_backend_availability(backend);
    if (!availability.available)
    {
        reject(backend, "create_camera_controller", "creating camera controllers through the factory", availability.reason);
    }
    return standard::create_controller(description);
}

} // namespace gfx::camera
