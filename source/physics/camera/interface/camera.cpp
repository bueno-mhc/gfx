// The library code of Camera (axes, turning, moving, looking at a point, culling, screen rays), the definitions that
// throw for operations a derived camera does not override, and the validation of camera descriptions.
module gfx.camera.interface;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.core.types;
import gfx.physics.enums;
import gfx.physics.primitives;

namespace gfx::camera
{

namespace
{

/// Throws the BackendCapabilityError of an operation @p backend's camera does not implement.
[[noreturn]] void
reject(BackendIdentity backend,
       std::string_view operation,
       std::string_view capability)
{
    throw BackendCapabilityError({.backend = backend,
                                  .operation = std::string{operation},
                                  .capability = std::string{capability},
                                  .reason = "this camera does not implement it",
                                  .suggestion = std::format("override {} in the camera class, or use the library's camera "
                                                            "(create_camera(description, CameraBackendKind::STANDARD))",
                                                            operation)});
}

/// Throws the InvalidUsageError of @p operation.
[[noreturn]] void
refuse(BackendIdentity backend,
       std::string_view operation,
       std::string problem,
       std::string suggestion)
{
    throw InvalidUsageError({.backend = backend, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::move(suggestion)});
}

/// Whether every component of @p vector is finite.
[[nodiscard]] auto
is_finite(Vector3 vector) noexcept -> bool
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

} // namespace

// ===================================================================================================== operations a camera implements

void
Camera::set_position(Vector3 /*position*/)
{
    reject(backend(), "Camera::set_position", "moving the camera");
}

void
Camera::set_orientation(const Quaternion& /*orientation*/)
{
    reject(backend(), "Camera::set_orientation", "turning the camera");
}

void
Camera::set_pose(Vector3 /*position*/,
                 const Quaternion& /*orientation*/)
{
    reject(backend(), "Camera::set_pose", "moving and turning the camera");
}

void
Camera::set_projection(const CameraProjection& projection)
{
    reject(backend(), "Camera::set_projection", std::format("{} projections", to_string(projection_kind(projection))));
}

void
Camera::set_clip_conventions(const ClipConventions& /*conventions*/)
{
    reject(backend(), "Camera::set_clip_conventions", "other clip conventions");
}

void
Camera::set_aspect_ratio(float /*aspect_ratio*/)
{
    reject(backend(), "Camera::set_aspect_ratio", "changing the aspect ratio");
}

// ===================================================================================================== library code

auto
Camera::transform() const noexcept -> Transform
{
    return {.position = position(), .orientation = orientation()};
}

auto
Camera::forward() const noexcept -> Vector3
{
    return gfx::rotate(orientation(), {.z = -1.0F});
}

auto
Camera::right() const noexcept -> Vector3
{
    return gfx::rotate(orientation(), {.x = 1.0F});
}

auto
Camera::up() const noexcept -> Vector3
{
    return gfx::rotate(orientation(), {.y = 1.0F});
}

auto
Camera::yaw_pitch_roll() const noexcept -> YawPitchRoll
{
    return to_yaw_pitch_roll(orientation());
}

void
Camera::set_orientation_from(YawPitchRoll angles)
{
    set_orientation(Quaternion::from_yaw_pitch_roll(angles));
}

void
Camera::rotate(Vector3 axis,
               float angle,
               ReferenceFrame frame)
{
    // A world axis turns the camera before its orientation (applied after it), a local one inside it.
    if (!is_finite(axis) || !(length_squared(axis) > 0.0F) || !std::isfinite(angle))
    {
        refuse(backend(), "Camera::rotate", "the axis must be a finite vector of non-zero length and the angle finite",
               "give a direction (it is normalized) and an angle in radians");
    }
    const Quaternion turn = Quaternion::from_axis_angle(normalized(axis), angle);
    set_orientation(normalized(frame == ReferenceFrame::WORLD ? turn * orientation() : orientation() * turn));
}

void
Camera::translate(Vector3 offset,
                  ReferenceFrame frame)
{
    set_position(position() + (frame == ReferenceFrame::WORLD ? offset : gfx::rotate(orientation(), offset)));
}

void
Camera::look_at(Vector3 target,
                Vector3 up)
{
    // A target at the camera gives no direction; an up vector of no length gives no roll.
    if (target == position())
    {
        refuse(backend(), "Camera::look_at", "the target is the camera's position: there is no direction to look along",
               "look at a point away from the camera, or set the orientation directly");
    }
    const std::optional<Quaternion> turned = look_at_orientation(position(), target, up);
    if (!turned)
    {
        refuse(backend(), "Camera::look_at", "the up vector has no direction", "give the world's up axis, e.g. {.y = 1}");
    }
    set_orientation(*turned);
}

auto
Camera::classify(const Aabb& box) const noexcept -> FrustumIntersection
{
    return physics::classify(frustum(), box);
}

auto
Camera::classify(const Sphere& sphere) const noexcept -> FrustumIntersection
{
    return physics::classify(frustum(), sphere);
}

auto
Camera::classify(const Obb& box) const noexcept -> FrustumIntersection
{
    return physics::classify(frustum(), box);
}

void
Camera::classify(std::span<const Aabb> boxes,
                 std::span<FrustumIntersection> output) const
{
    physics::classify(frustum(), boxes, output);
}

void
Camera::classify(std::span<const Sphere> spheres,
                 std::span<FrustumIntersection> output) const
{
    physics::classify(frustum(), spheres, output);
}

auto
Camera::screen_to_world_ray(Vector2 point,
                            const ScreenViewport& viewport) const noexcept -> Ray
{
    return camera::screen_to_world_ray(inverse_view_projection(), point, viewport, clip_conventions());
}

auto
Camera::world_to_screen(Vector3 point,
                        const ScreenViewport& viewport) const noexcept -> std::optional<Vector3>
{
    return camera::world_to_screen(view_projection(), point, viewport, clip_conventions());
}

// ===================================================================================================== validation

void
validate_camera_description(const CameraDescription& description,
                            std::string_view operation,
                            BackendIdentity backend)
{
    if (const std::optional<CameraProblem> problem = camera_problem(description))
    {
        refuse(backend, operation, std::format("{} (it is {})", problem->problem, problem->value), std::string{problem->suggestion});
    }
}

} // namespace gfx::camera
