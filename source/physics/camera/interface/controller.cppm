/// @file controller.cppm
/// @brief Camera controllers: what turns input into camera motion — free fly, first person, orbit. A controller reads
///        plain values (CameraControllerInput: movement, turning, roll, zoom, elapsed time), never the platform
///        library's events, which the application translates (mouse deltas times its sensitivity, keys to movement), so
///        the camera stays independent of any input system. Limits (pitch, distance) live in the controller.
///
/// The library's controllers come from create_camera_controller() (gfx.camera.factory). Like Camera, each operation
/// that can fail is pure virtual with a definition that throws a descriptive BackendCapabilityError.
export module gfx.camera.interface:controller;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.core.types;
import gfx.physics.enums;
import :camera;

export namespace gfx::camera
{

/// @brief One update's input to a controller.
struct CameraControllerInput
{
    Vector3 movement{};   ///< Direction to move: +X right, +Y up, +Z forward (components usually in [-1, 1]); scaled by speed × seconds.
    Vector2 look{};       ///< Turning in radians: x yaw (positive turns right), y pitch (positive looks up).
    float roll = 0.0F;    ///< Roll in radians, positive banks right (FREE_FLY only).
    float zoom = 0.0F;    ///< Zoom steps, positive toward the target (ORBIT only): distance × exp(-zoom × zoom_rate).
    float seconds = 0.0F; ///< Time the input spans, at least 0.
};

/// @brief A free-fly controller: turns about the camera's own axes, moves along them (six degrees of freedom).
struct FreeFlyControllerDescription
{
    float speed = 5.0F; ///< Units per second at a movement of length 1.
};

/// @brief A first-person controller: yaw about the world's up axis, pitch within limits, no roll; moves on the plane
///        perpendicular to the up axis (+Y movement along it).
struct FirstPersonControllerDescription
{
    float speed = 5.0F;           ///< Units per second at a movement of length 1.
    Vector3 up{.y = 1.0F};        ///< The world's up axis (any length but 0).
    float minimum_pitch = -1.55F; ///< Lowest pitch in radians, greater than -π/2.
    float maximum_pitch = 1.55F;  ///< Highest pitch in radians, less than π/2 and at least minimum_pitch.
};

/// @brief An orbit controller: the camera looks at a target from a distance, turned by yaw and pitch (the camera's own:
///        a negative pitch looks down at the target from above); zoom changes the distance, movement pans the target.
struct OrbitControllerDescription
{
    Vector3 target{};               ///< The point looked at.
    float distance = 5.0F;          ///< From the target, in [minimum_distance, maximum_distance].
    float yaw = 0.0F;               ///< Yaw of the camera in radians (about the up axis).
    float pitch = -0.4F;            ///< Pitch of the camera in radians, in [minimum_pitch, maximum_pitch].
    float minimum_distance = 0.1F;  ///< Closest distance, greater than 0.
    float maximum_distance = 1000.0F; ///< Farthest distance, finite and at least minimum_distance.
    float minimum_pitch = -1.55F;   ///< Lowest pitch, greater than -π/2.
    float maximum_pitch = 1.55F;    ///< Highest pitch, less than π/2 and at least minimum_pitch.
    float zoom_rate = 0.1F;         ///< Natural logarithm of the distance's change per zoom step, at least 0.
    float pan_speed = 1.0F;         ///< Target movement per second at a movement of length 1, as a fraction of the distance.
    Vector3 up{.y = 1.0F};          ///< The world's up axis (any length but 0).
};

/// @brief A controller's description: its kind and parameters (and, for ORBIT, its state).
using CameraControllerDescription = std::variant<FreeFlyControllerDescription, FirstPersonControllerDescription, OrbitControllerDescription>;

/// @brief The kind of controller @p description describes.
[[nodiscard]] constexpr auto
controller_kind(const CameraControllerDescription& description) noexcept -> CameraControllerKind
{
    return static_cast<CameraControllerKind>(description.index());
}

/// @brief A camera controller.
class CameraController
{
public:
    virtual ~CameraController() = default;

    CameraController(const CameraController&) = delete;
    auto operator=(const CameraController&) -> CameraController& = delete;

    /// @brief The implementation (errors name it).
    [[nodiscard]] auto backend() const noexcept -> BackendIdentity
    {
        return m_backend;
    }

    /// @brief The kind of controller.
    [[nodiscard]] virtual auto kind() const noexcept -> CameraControllerKind = 0;

    /// @brief Its parameters and state now (an orbit controller's target, distance, yaw and pitch after the updates).
    [[nodiscard]] virtual auto description() const noexcept -> CameraControllerDescription = 0;

    /// @brief Replaces its parameters and state.
    /// @throws InvalidUsageError if @p description is of another kind or invalid.
    virtual void set_description(const CameraControllerDescription& description) = 0;

    /// @brief Moves and turns @p camera by @p input.
    /// @throws InvalidUsageError if @p input holds a value that is not finite or a negative time.
    virtual void update(Camera& camera,
                        const CameraControllerInput& input) = 0;

protected:
    /// @param backend The implementation, as errors name it.
    explicit CameraController(BackendIdentity backend) noexcept
        : m_backend{backend}
    {
    }

private:
    BackendIdentity m_backend;
};

/// @brief Throws InvalidUsageError if @p description is invalid (speeds, limits, distances, axes), naming
///        @p operation and @p backend.
void validate_controller_description(const CameraControllerDescription& description,
                                     std::string_view operation,
                                     BackendIdentity backend);

/// @brief Throws InvalidUsageError if @p input holds a value that is not finite or a negative time.
void validate_controller_input(const CameraControllerInput& input,
                               std::string_view operation,
                               BackendIdentity backend);

} // namespace gfx::camera
