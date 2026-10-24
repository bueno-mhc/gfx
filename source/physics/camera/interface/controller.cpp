// The definitions that throw for operations a derived camera controller does not override, and the validation of
// controller descriptions and inputs.
module gfx.camera.interface;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.enums;

namespace gfx::camera
{

namespace
{

/// Throws the InvalidUsageError of @p operation.
[[noreturn]] void
refuse(BackendIdentity backend,
       std::string_view operation,
       std::string problem,
       std::string suggestion)
{
    throw InvalidUsageError({.backend = backend, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::move(suggestion)});
}

/// The checks of a description: each refuses @p operation with what is wrong.
class DescriptionCheck
{
public:
    DescriptionCheck(BackendIdentity backend,
                     std::string_view operation,
                     std::string_view kind)
        : m_backend{backend}
        , m_operation{operation}
        , m_kind{kind}
    {
    }

    /// Refuses a value that is not finite and at least @p minimum.
    void at_least(std::string_view name,
                  float value,
                  float minimum) const
    {
        if (!std::isfinite(value) || !(value >= minimum))
        {
            refuse(m_backend, m_operation, std::format("{}: {} must be finite and at least {} (it is {})", m_kind, name, minimum, value),
                   "give a finite value in range");
        }
    }

    /// Refuses a value that is not finite or not greater than @p minimum.
    void greater_than(std::string_view name,
                      float value,
                      float minimum) const
    {
        if (!std::isfinite(value) || !(value > minimum))
        {
            refuse(m_backend, m_operation, std::format("{}: {} must be finite and greater than {} (it is {})", m_kind, name, minimum, value),
                   "give a finite value in range");
        }
    }

    /// Refuses a vector that is not finite or (when @p direction) has no length.
    void vector(std::string_view name,
                Vector3 value,
                bool direction) const
    {
        const bool finite = std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        if (!finite || (direction && !(length_squared(value) > 0.0F)))
        {
            refuse(m_backend, m_operation,
                   std::format("{}: {} must be finite{} (it is ({}, {}, {}))", m_kind, name, direction ? " and of non-zero length" : "", value.x,
                               value.y, value.z),
                   direction ? "give a direction, e.g. {.y = 1} for +Y up (it is normalized)" : "give finite coordinates");
        }
    }

    /// Refuses pitch limits outside (-π/2, π/2) or out of order.
    void pitch_limits(float minimum,
                      float maximum) const
    {
        constexpr float QUARTER_TURN = std::numbers::pi_v<float> * 0.5F;
        if (!(minimum > -QUARTER_TURN) || !(maximum < QUARTER_TURN) || !(minimum <= maximum))
        {
            refuse(m_backend, m_operation,
                   std::format("{}: the pitch limits must satisfy -π/2 < minimum_pitch <= maximum_pitch < π/2 (they are {} and {})", m_kind, minimum,
                               maximum),
                   "keep the pitch short of straight up and down, where the yaw axis and the view direction meet (1.55 is about 89°)");
        }
    }

    /// Refuses a value outside [@p minimum, @p maximum].
    void within(std::string_view name,
                float value,
                float minimum,
                float maximum) const
    {
        if (!std::isfinite(value) || !(value >= minimum && value <= maximum))
        {
            refuse(m_backend, m_operation, std::format("{}: {} must be in [{}, {}] (it is {})", m_kind, name, minimum, maximum, value),
                   "keep the state within the description's limits");
        }
    }

private:
    BackendIdentity m_backend;
    std::string_view m_operation;
    std::string_view m_kind;
};

} // namespace

// ===================================================================================================== operations a controller implements

void
CameraController::set_description(const CameraControllerDescription& description)
{
    throw BackendCapabilityError({.backend = backend(),
                                  .operation = "CameraController::set_description",
                                  .capability = std::format("{} controllers", to_string(controller_kind(description))),
                                  .reason = "this controller does not implement it",
                                  .suggestion = "override CameraController::set_description in the controller class"});
}

void
CameraController::update(Camera& /*camera*/,
                         const CameraControllerInput& /*input*/)
{
    throw BackendCapabilityError({.backend = backend(),
                                  .operation = "CameraController::update",
                                  .capability = std::format("{} control", to_string(kind())),
                                  .reason = "this controller does not implement it",
                                  .suggestion = "override CameraController::update in the controller class"});
}

// ===================================================================================================== validation

void
validate_controller_description(const CameraControllerDescription& description,
                                std::string_view operation,
                                BackendIdentity backend)
{
    // Speeds and rates at least 0, axes with a direction, pitch short of the poles, distances in order.
    const DescriptionCheck check{backend, operation, to_string(controller_kind(description))};
    if (const auto* free_fly = std::get_if<FreeFlyControllerDescription>(&description))
    {
        check.at_least("speed", free_fly->speed, 0.0F);
    }
    else if (const auto* first_person = std::get_if<FirstPersonControllerDescription>(&description))
    {
        check.at_least("speed", first_person->speed, 0.0F);
        check.vector("up", first_person->up, true);
        check.pitch_limits(first_person->minimum_pitch, first_person->maximum_pitch);
    }
    else
    {
        const auto& orbit = std::get<OrbitControllerDescription>(description);
        check.vector("target", orbit.target, false);
        check.vector("up", orbit.up, true);
        check.greater_than("minimum_distance", orbit.minimum_distance, 0.0F);
        check.at_least("maximum_distance", orbit.maximum_distance, orbit.minimum_distance);
        check.within("distance", orbit.distance, orbit.minimum_distance, orbit.maximum_distance);
        check.pitch_limits(orbit.minimum_pitch, orbit.maximum_pitch);
        check.within("pitch", orbit.pitch, orbit.minimum_pitch, orbit.maximum_pitch);
        check.at_least("yaw", orbit.yaw, std::numeric_limits<float>::lowest());
        check.at_least("zoom_rate", orbit.zoom_rate, 0.0F);
        check.at_least("pan_speed", orbit.pan_speed, 0.0F);
    }
}

void
validate_controller_input(const CameraControllerInput& input,
                          std::string_view operation,
                          BackendIdentity backend)
{
    const bool finite = std::isfinite(input.movement.x) && std::isfinite(input.movement.y) && std::isfinite(input.movement.z) && std::isfinite(input.look.x)
                        && std::isfinite(input.look.y) && std::isfinite(input.roll) && std::isfinite(input.zoom);
    if (!finite || !std::isfinite(input.seconds) || !(input.seconds >= 0.0F))
    {
        refuse(backend, operation,
               std::format("the input must be finite with a time of at least 0 (movement ({}, {}, {}), look ({}, {}), roll {}, zoom {}, seconds {})",
                           input.movement.x, input.movement.y, input.movement.z, input.look.x, input.look.y, input.roll, input.zoom, input.seconds),
               "pass the time since the last update and finite deltas (a NaN often comes from 0 / 0 in a sensitivity computation)");
    }
}

} // namespace gfx::camera
