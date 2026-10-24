/// @file camera_factory.cppm
/// @brief The only public entry to the library's camera implementations: what is available, and creation.
///
/// An application's own camera (an off-axis stereo pair, an oblique near plane, a jittered projection) derives from
/// gfx::camera::Camera and is constructed directly; the factory builds the library's cameras only.
export module gfx.camera.factory;

import std;
import gfx.physics.enums;
import gfx.camera.interface;

export namespace gfx::camera
{

/// @brief Whether a camera implementation can be created, and why not.
struct CameraBackendAvailability
{
    CameraBackendKind backend = CameraBackendKind::STANDARD; ///< The implementation.
    bool built = false;                                      ///< Compiled into the library.
    bool available = false;                                  ///< create_camera() can succeed.
    std::string reason{};                                    ///< Why it is unavailable (empty when available).
};

/// @brief Whether @p backend's cameras can be created.
[[nodiscard]] auto camera_backend_availability(CameraBackendKind backend) -> CameraBackendAvailability;

/// @brief The implementations create_camera() can build.
[[nodiscard]] auto available_camera_backends() -> std::vector<CameraBackendKind>;

/// @brief A camera of @p description.
/// @throws InvalidUsageError if @p description is invalid (camera_problem() names the problem).
/// @throws BackendCapabilityError if @p backend is unavailable (CUSTOM: construct the camera class directly).
[[nodiscard]] auto create_camera(const CameraDescription& description = {},
                                 CameraBackendKind backend = CameraBackendKind::STANDARD) -> std::unique_ptr<Camera>;

/// @brief A camera controller of @p description.
/// @throws InvalidUsageError if @p description is invalid.
/// @throws BackendCapabilityError if @p backend is unavailable.
[[nodiscard]] auto create_camera_controller(const CameraControllerDescription& description,
                                            CameraBackendKind backend = CameraBackendKind::STANDARD) -> std::unique_ptr<CameraController>;

} // namespace gfx::camera
