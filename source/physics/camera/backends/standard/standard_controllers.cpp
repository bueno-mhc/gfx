// The standard camera controllers: free fly (turns about the camera's own axes), first person (yaw about the world's
// up axis, clamped pitch, no roll) and orbit (around a target). The first-person and orbit controllers measure yaw and
// pitch in the frame that takes +Y to their up axis.
module gfx.camera.backends.standard;

import std;
import gfx.core.errors;
import gfx.camera.interface;

namespace gfx::camera::standard
{

namespace
{

/// The rotation taking +Y to the unit vector @p up: the frame yaw and pitch are measured in.
[[nodiscard]] auto
up_frame(Vector3 up) noexcept -> Quaternion
{
    return Quaternion::from_rotation_between({.y = 1.0F}, up);
}

/// The world-space offset of @p movement (+X right, +Y up, +Z forward) along the axes @p right, @p up and @p forward.
[[nodiscard]] auto
offset_of(Vector3 movement,
          Vector3 right,
          Vector3 up,
          Vector3 forward,
          float scale) noexcept -> Vector3
{
    return (right * movement.x + up * movement.y + forward * movement.z) * scale;
}

/// Throws the InvalidUsageError of a description of another kind.
[[noreturn]] void
refuse_kind(BackendIdentity backend,
            CameraControllerKind expected,
            const CameraControllerDescription& description)
{
    throw InvalidUsageError({.backend = backend,
                             .operation = "CameraController::set_description",
                             .problem = std::format("the controller is a {} controller; the description is of a {} controller", to_string(expected),
                                                    to_string(controller_kind(description))),
                             .suggestion = "create a controller of that kind with create_camera_controller()"});
}

/// The base of the standard controllers: the description of one kind, validated.
template<class Description, CameraControllerKind KIND>
class StandardController : public CameraController
{
public:
    explicit StandardController(const Description& description)
        : CameraController(CameraBackendKind::STANDARD)
        , m_description{description}
    {
        validate_controller_description(m_description, "create_camera_controller", backend());
    }

    [[nodiscard]] auto kind() const noexcept -> CameraControllerKind override
    {
        return KIND;
    }

    [[nodiscard]] auto description() const noexcept -> CameraControllerDescription override
    {
        return m_description;
    }

    void set_description(const CameraControllerDescription& description) override
    {
        const auto* replacement = std::get_if<Description>(&description);
        if (replacement == nullptr)
        {
            refuse_kind(backend(), KIND, description);
        }
        validate_controller_description(description, "CameraController::set_description", backend());
        m_description = *replacement;
    }

protected:
    Description m_description{};
};

/// Free fly: yaw about the camera's +Y, pitch about its +X, roll about its -Z; movement along its axes.
class FreeFlyController final : public StandardController<FreeFlyControllerDescription, CameraControllerKind::FREE_FLY>
{
public:
    using StandardController::StandardController;

    void update(Camera& camera,
                const CameraControllerInput& input) override
    {
        // Turn inside the current orientation (a positive look.x turns right: a negative angle about +Y), then move
        // along the turned axes.
        validate_controller_input(input, "CameraController::update", backend());
        const Quaternion turn = Quaternion::from_axis_angle({.y = 1.0F}, -input.look.x) * Quaternion::from_axis_angle({.x = 1.0F}, input.look.y)
                                * Quaternion::from_axis_angle({.z = -1.0F}, input.roll);
        const Quaternion orientation = normalized(camera.orientation() * turn);
        const Vector3 offset = offset_of(input.movement, rotate(orientation, {.x = 1.0F}), rotate(orientation, {.y = 1.0F}),
                                         rotate(orientation, {.z = -1.0F}), m_description.speed * input.seconds);
        camera.set_pose(camera.position() + offset, orientation);
    }
};

/// First person: yaw about the up axis, clamped pitch, no roll; movement on the plane perpendicular to the up axis.
class FirstPersonController final : public StandardController<FirstPersonControllerDescription, CameraControllerKind::FIRST_PERSON>
{
public:
    using StandardController::StandardController;

    void update(Camera& camera,
                const CameraControllerInput& input) override
    {
        // The camera's yaw and pitch in the up axis's frame (its roll is dropped), turned and clamped.
        validate_controller_input(input, "CameraController::update", backend());
        const Vector3 up = normalized(m_description.up);
        const Quaternion frame = up_frame(up);
        const YawPitchRoll current = to_yaw_pitch_roll(normalized(conjugate(frame) * camera.orientation()));
        const float yaw = current.yaw - input.look.x;
        const float pitch = std::clamp(current.pitch + input.look.y, m_description.minimum_pitch, m_description.maximum_pitch);
        const Quaternion heading = frame * Quaternion::from_axis_angle({.y = 1.0F}, yaw);
        const Quaternion orientation = normalized(heading * Quaternion::from_axis_angle({.x = 1.0F}, pitch));

        // Forward and right on the horizontal plane, whatever the pitch; +Y movement along the up axis.
        const Vector3 offset = offset_of(input.movement, rotate(heading, {.x = 1.0F}), up, rotate(heading, {.z = -1.0F}),
                                         m_description.speed * input.seconds);
        camera.set_pose(camera.position() + offset, orientation);
    }
};

/// Orbit: the camera at the distance from the target, turned by the controller's yaw and pitch, looking at it.
class OrbitController final : public StandardController<OrbitControllerDescription, CameraControllerKind::ORBIT>
{
public:
    using StandardController::StandardController;

    void update(Camera& camera,
                const CameraControllerInput& input) override
    {
        // Turn, zoom (a positive zoom shortens the distance), then pan the target along the turned camera's axes.
        validate_controller_input(input, "CameraController::update", backend());
        OrbitControllerDescription& state = m_description;
        state.yaw -= input.look.x;
        state.pitch = std::clamp(state.pitch + input.look.y, state.minimum_pitch, state.maximum_pitch);
        state.distance = std::clamp(state.distance * std::exp(-input.zoom * state.zoom_rate), state.minimum_distance, state.maximum_distance);
        const Quaternion orientation = normalized(up_frame(normalized(state.up)) * Quaternion::from_yaw_pitch_roll({.yaw = state.yaw, .pitch = state.pitch}));
        const Vector3 forward = rotate(orientation, {.z = -1.0F});
        state.target += offset_of(input.movement, rotate(orientation, {.x = 1.0F}), rotate(orientation, {.y = 1.0F}), forward,
                                  state.pan_speed * state.distance * input.seconds);
        camera.set_pose(state.target - forward * state.distance, orientation);
    }
};

} // namespace

auto
create_controller(const CameraControllerDescription& description) -> std::unique_ptr<CameraController>
{
    if (const auto* free_fly = std::get_if<FreeFlyControllerDescription>(&description))
    {
        return std::make_unique<FreeFlyController>(*free_fly);
    }
    if (const auto* first_person = std::get_if<FirstPersonControllerDescription>(&description))
    {
        return std::make_unique<FirstPersonController>(*first_person);
    }
    return std::make_unique<OrbitController>(std::get<OrbitControllerDescription>(description));
}

} // namespace gfx::camera::standard
