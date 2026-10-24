/// @file standard.cppm
/// @brief The library's camera and camera controllers (CameraBackendKind::STANDARD), reached only through
///        gfx.camera.factory: this module is in a private file set. Its classes are declared in its implementation
///        units.
export module gfx.camera.backends.standard;

import std;
import gfx.camera.interface;

export namespace gfx::camera::standard
{

/// @brief Why the standard cameras cannot be created (empty: they always can).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief A standard camera of @p description.
/// @throws InvalidUsageError if @p description is invalid.
[[nodiscard]] auto create_camera(const CameraDescription& description) -> std::unique_ptr<Camera>;

/// @brief A standard controller of @p description.
/// @throws InvalidUsageError if @p description is invalid.
[[nodiscard]] auto create_controller(const CameraControllerDescription& description) -> std::unique_ptr<CameraController>;

} // namespace gfx::camera::standard
