// The checks of every value a world records: descriptions, poses, vectors, materials, mass properties, joints, soft
// bodies and settings. Each throws a PhysicsError naming the value, what is wrong and what to give instead; the checked
// values come back normalized (orientations and axes of unit length).
module gfx.physics.world;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.interface;
import :state;

namespace gfx::physics
{

namespace
{

[[nodiscard]] auto
finite(Vector3 value) noexcept -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] auto
finite(const Matrix3& value) noexcept -> bool
{
    return finite(value.columns[0]) && finite(value.columns[1]) && finite(value.columns[2]);
}

/// @p axis of unit length.
[[nodiscard]] auto
checked_axis(Vector3 axis,
             const char* what,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context) -> Vector3
{
    check_vector(axis, what, backend, operation, context);
    if (!(length_squared(axis) > 0.0F))
    {
        reject(backend, operation, std::format("{} is the zero vector", what), "give a direction", context);
    }
    return normalized(axis);
}

/// Limits in order and finite.
void
check_limits(const JointLimits& limits,
             const char* what,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context)
{
    check_finite(limits.lower, what, backend, operation, context);
    check_finite(limits.upper, what, backend, operation, context);
    if (limits.lower > limits.upper)
    {
        reject(backend, operation, std::format("{}: the lower limit {} is above the upper limit {}", what, limits.lower, limits.upper), "give lower ≤ upper", context);
    }
}

void
check_motor(const JointMotor& motor,
            const BackendIdentity& backend,
            std::string_view operation,
            const PhysicsErrorContext& context)
{
    check_finite(motor.target_speed, "the motor's target speed", backend, operation, context);
    check_non_negative(motor.maximum_force, "the motor's maximum force", backend, operation, context);
}

void
check_spring(const JointSpring& spring,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context)
{
    check_non_negative(spring.frequency, "the spring's frequency", backend, operation, context);
    check_non_negative(spring.damping_ratio, "the spring's damping ratio", backend, operation, context);
}

void
check_breaking(const JointBreaking& breaking,
               const BackendIdentity& backend,
               std::string_view operation,
               const PhysicsErrorContext& context)
{
    // Infinite thresholds never break; finite ones must be positive.
    if (!(breaking.force > 0.0F) || !(breaking.torque > 0.0F))
    {
        reject(backend, operation, std::format("the breaking force {} and torque {} must be greater than 0 (infinite: never)", breaking.force, breaking.torque),
               "give positive thresholds, or leave them infinite", context);
    }
}

/// The joint's bodies: a first one, not joined to itself.
template<class Joint>
void
check_bodies(const Joint& joint,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context)
{
    if (!joint.first)
    {
        reject(backend, operation, "the joint's first body is null", "give the first body (the second may be null: the world)", context);
    }
    if (joint.first == joint.second)
    {
        reject(backend, operation, std::format("the joint joins {} to itself", joint.first), "join two different bodies, or a body and the world (second null)", context);
    }
}

} // namespace

void
reject(const BackendIdentity& backend,
       std::string_view operation,
       std::string problem,
       std::string suggestion,
       const PhysicsErrorContext& context)
{
    throw PhysicsError({.backend = backend, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::move(suggestion), .context = context});
}

auto
checked_pose(const Transform& pose,
             const char* what,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context) -> Transform
{
    check_vector(pose.position, what, backend, operation, context);
    const float norm = length(pose.orientation);
    if (!std::isfinite(norm) || !(norm > 0.0F))
    {
        reject(backend, operation,
               std::format("{} has an orientation ({}, {}, {}, {}) that is not a finite, non-zero quaternion", what, pose.orientation.x, pose.orientation.y,
                           pose.orientation.z, pose.orientation.w),
               "give a rotation (a unit quaternion; any non-zero one is normalized)", context);
    }
    return {.position = pose.position, .orientation = normalized(pose.orientation)};
}

void
check_vector(Vector3 value,
             const char* what,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context)
{
    if (!finite(value))
    {
        reject(backend, operation, std::format("{} ({}, {}, {}) is not finite", what, value.x, value.y, value.z), "give finite values", context);
    }
}

void
check_non_negative(float value,
                   const char* what,
                   const BackendIdentity& backend,
                   std::string_view operation,
                   const PhysicsErrorContext& context)
{
    if (!(value >= 0.0F) || !std::isfinite(value))
    {
        reject(backend, operation, std::format("{} is {}: not finite and at least 0", what, value), "give a finite value of at least 0", context);
    }
}

void
check_finite(float value,
             const char* what,
             const BackendIdentity& backend,
             std::string_view operation,
             const PhysicsErrorContext& context)
{
    if (!std::isfinite(value))
    {
        reject(backend, operation, std::format("{} is {}: not finite", what, value), "give a finite value", context);
    }
}

void
check_mass_properties(const MassProperties& properties,
                      const BackendIdentity& backend,
                      std::string_view operation,
                      const PhysicsErrorContext& context)
{
    if (!(properties.mass > 0.0F) || !std::isfinite(properties.mass))
    {
        reject(backend, operation, std::format("the mass {} is not finite and positive", properties.mass),
               "give a positive mass, or std::nullopt for the colliders' own", context);
    }
    check_vector(properties.center_of_mass, "the center of mass", backend, operation, context);
    if (!finite(properties.inertia))
    {
        reject(backend, operation, "the inertia tensor is not finite", "give a finite inertia tensor about the center of mass", context);
    }
}

void
check_material(const SurfaceMaterial& material,
               const BackendIdentity& backend,
               std::string_view operation,
               const PhysicsErrorContext& context)
{
    check_non_negative(material.friction.static_coefficient, "the static friction coefficient", backend, operation, context);
    check_non_negative(material.friction.dynamic_coefficient, "the dynamic friction coefficient", backend, operation, context);
    check_non_negative(material.friction.rolling, "the rolling resistance", backend, operation, context);
    if (!(material.restitution >= 0.0F && material.restitution <= 1.0F))
    {
        reject(backend, operation, std::format("the restitution {} is outside [0, 1]", material.restitution), "give a restitution in [0, 1]", context);
    }
}

auto
checked_description(const RigidBodyDescription& description,
                    const BackendIdentity& backend,
                    std::string_view operation,
                    const PhysicsErrorContext& context) -> RigidBodyDescription
{
    RigidBodyDescription checked = description;
    checked.pose = checked_pose(description.pose, "the body's pose", backend, operation, context);
    check_vector(description.linear_velocity, "the linear velocity", backend, operation, context);
    check_vector(description.angular_velocity, "the angular velocity", backend, operation, context);
    check_non_negative(description.linear_damping, "the linear damping", backend, operation, context);
    check_non_negative(description.angular_damping, "the angular damping", backend, operation, context);
    check_finite(description.gravity_scale, "the gravity scale", backend, operation, context);
    if (description.mass_properties)
    {
        check_mass_properties(*description.mass_properties, backend, operation, context);
    }
    return checked;
}

auto
checked_description(const ColliderDescription& description,
                    const BackendIdentity& backend,
                    std::string_view operation,
                    const PhysicsErrorContext& context) -> ColliderDescription
{
    if (!description.shape)
    {
        reject(backend, operation, "the collider has no shape", "give a shape (make_shape(...))", context);
    }
    ColliderDescription checked = description;
    checked.pose = checked_pose(description.pose, "the collider's pose", backend, operation, context);
    check_material(description.material, backend, operation, context);
    check_non_negative(description.density, "the density", backend, operation, context);
    if (description.round_shape_segments < 3)
    {
        reject(backend, operation, std::format("{} segments cannot approximate a round shape", description.round_shape_segments), "give at least 3 segments",
               context);
    }
    return checked;
}

auto
checked_description(const TriggerDescription& description,
                    const BackendIdentity& backend,
                    std::string_view operation,
                    const PhysicsErrorContext& context) -> TriggerDescription
{
    if (!description.shape)
    {
        reject(backend, operation, "the trigger has no shape", "give a shape (make_shape(...))", context);
    }
    TriggerDescription checked = description;
    checked.pose = checked_pose(description.pose, "the trigger's pose", backend, operation, context);
    return checked;
}

auto
checked_description(const JointDescription& description,
                    const BackendIdentity& backend,
                    std::string_view operation,
                    const PhysicsErrorContext& context) -> JointDescription
{
    JointDescription checked = description;
    std::visit(
        [&](auto& joint) {
            using Joint = std::remove_cvref_t<decltype(joint)>;
            check_bodies(joint, backend, operation, context);
            check_breaking(joint.breaking, backend, operation, context);
            if constexpr (std::same_as<Joint, DistanceJointDescription>)
            {
                check_vector(joint.anchor_first, "the first anchor", backend, operation, context);
                check_vector(joint.anchor_second, "the second anchor", backend, operation, context);
                check_non_negative(joint.length, "the length", backend, operation, context);
                check_non_negative(joint.minimum_length, "the minimum length", backend, operation, context);
                if (!(joint.maximum_length >= joint.minimum_length))
                {
                    reject(backend, operation, std::format("the maximum length {} is below the minimum length {}", joint.maximum_length, joint.minimum_length),
                           "give minimum ≤ maximum", context);
                }
                check_spring(joint.spring, backend, operation, context);
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription> || std::same_as<Joint, SliderJointDescription>)
            {
                check_vector(joint.anchor, "the anchor", backend, operation, context);
                joint.axis = checked_axis(joint.axis, "the axis", backend, operation, context);
                if (joint.limits)
                {
                    check_limits(*joint.limits, "the limits", backend, operation, context);
                }
                if (joint.motor)
                {
                    check_motor(*joint.motor, backend, operation, context);
                }
                check_spring(joint.spring, backend, operation, context);
            }
            else if constexpr (std::same_as<Joint, BallAndSocketJointDescription>)
            {
                check_vector(joint.anchor, "the anchor", backend, operation, context);
            }
            else if constexpr (std::same_as<Joint, FixedJointDescription>)
            {
                check_vector(joint.anchor, "the anchor", backend, operation, context);
                check_spring(joint.linear_spring, backend, operation, context);
                check_spring(joint.angular_spring, backend, operation, context);
            }
            else if constexpr (std::same_as<Joint, ConeTwistJointDescription>)
            {
                check_vector(joint.anchor, "the anchor", backend, operation, context);
                joint.axis = checked_axis(joint.axis, "the twist axis", backend, operation, context);
                if (!(joint.swing_limit > 0.0F && joint.swing_limit < std::numbers::pi_v<float>))
                {
                    reject(backend, operation, std::format("the swing limit {} is outside (0, π)", joint.swing_limit), "give a cone half angle between 0 and π", context);
                }
                if (joint.twist_limits)
                {
                    check_limits(*joint.twist_limits, "the twist limits", backend, operation, context);
                }
            }
            else
            {
                joint.frame = checked_pose(joint.frame, "the joint's frame", backend, operation, context);
                for (std::size_t axis = 0; axis < joint.motion.size(); ++axis)
                {
                    if (joint.motion[axis] == AxisMotion::LIMITED)
                    {
                        check_limits(joint.limits[axis], "an axis' limits", backend, operation, context);
                    }
                    if (joint.motors[axis])
                    {
                        check_motor(*joint.motors[axis], backend, operation, context);
                    }
                }
            }
        },
        checked);
    return checked;
}

void
check_description(const ConstraintDescription& description,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context)
{
    // Two bodies (or one and the world), its rows, when it breaks.
    check_bodies(description, backend, operation, context);
    check_breaking(description.breaking, backend, operation, context);
    if (description.constraint == nullptr)
    {
        reject(backend, operation, "the user constraint is null", "give the constraint whose rows the solver keeps", context);
    }
    const std::uint32_t rows = description.constraint->row_count();
    if (rows == 0 || rows > MAXIMUM_CONSTRAINT_ROWS)
    {
        reject(backend, operation, std::format("the user constraint has {} rows", rows), std::format("give between 1 and {} rows", MAXIMUM_CONSTRAINT_ROWS), context);
    }
}

void
check_pin(const ParticlePin& pin,
          std::uint32_t particles,
          const BackendIdentity& backend,
          std::string_view operation,
          const PhysicsErrorContext& context)
{
    if (pin.particle >= particles)
    {
        reject(backend, operation, std::format("the pin holds particle {} of {}", pin.particle, particles), "pin a particle of the soft body", context);
    }
    check_vector(pin.point, "the pin's point", backend, operation, context);
    check_non_negative(pin.compliance, "the pin's compliance", backend, operation, context);
}

void
check_description(const SoftBodyDescription& description,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context)
{
    // Particles, constraints between them, pins.
    const auto count = static_cast<std::uint32_t>(description.particles.size());
    if (count == 0)
    {
        reject(backend, operation, "the soft body has no particle", "give at least one particle", context);
    }
    for (const SoftBodyParticle& particle : description.particles)
    {
        check_vector(particle.position, "a particle's position", backend, operation, context);
        check_vector(particle.velocity, "a particle's velocity", backend, operation, context);
        check_non_negative(particle.inverse_mass, "a particle's inverse mass", backend, operation, context);
    }
    for (std::size_t index = 0; index < description.constraints.size(); ++index)
    {
        const SoftBodyConstraint& constraint = description.constraints[index];
        const std::uint32_t used = soft_body_constraint_particles(constraint.kind);
        for (std::uint32_t corner = 0; corner < used; ++corner)
        {
            if (constraint.particles[corner] >= count)
            {
                reject(backend, operation, std::format("constraint {} ({}) names particle {} of {}", index, constraint.kind, constraint.particles[corner], count),
                       "name particles of the soft body", context);
            }
            for (std::uint32_t other = 0; other < corner; ++other)
            {
                if (constraint.particles[other] == constraint.particles[corner])
                {
                    reject(backend, operation, std::format("constraint {} ({}) names particle {} twice", index, constraint.kind, constraint.particles[corner]),
                           "name different particles", context);
                }
            }
        }
        check_non_negative(constraint.compliance, "a constraint's compliance", backend, operation, context);
        if (constraint.rest_value)
        {
            if (constraint.kind == SoftBodyConstraintKind::DIHEDRAL)
            {
                check_finite(*constraint.rest_value, "a constraint's rest angle", backend, operation, context);
            }
            else
            {
                check_non_negative(*constraint.rest_value, "a constraint's rest value", backend, operation, context);
            }
        }
    }
    for (const ParticlePin& pin : description.pins)
    {
        check_pin(pin, count, backend, operation, context);
    }

    // Its settings.
    if (!(description.particle_radius > 0.0F) || !std::isfinite(description.particle_radius))
    {
        reject(backend, operation, std::format("the particle radius {} is not finite and positive", description.particle_radius), "give a positive radius", context);
    }
    if (!(description.damping >= 0.0F && description.damping <= 1.0F))
    {
        reject(backend, operation, std::format("the damping {} is outside [0, 1]", description.damping), "give a damping in [0, 1]", context);
    }
    check_non_negative(description.friction, "the friction", backend, operation, context);
    check_finite(description.gravity_scale, "the gravity scale", backend, operation, context);
    if (description.sub_steps == 0)
    {
        reject(backend, operation, "the soft body has no sub-step", "give at least one sub-step", context);
    }
}

void
check_description(const SolverDescription& description,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context)
{
    if (description.sub_steps == 0 || description.velocity_iterations == 0)
    {
        reject(backend, operation, std::format("the solver has {} sub-steps and {} velocity iterations", description.sub_steps, description.velocity_iterations),
               "give at least one sub-step and one velocity iteration", context);
    }
    check_non_negative(description.restitution_threshold, "the restitution threshold", backend, operation, context);
    check_non_negative(description.static_friction_speed, "the static friction speed", backend, operation, context);
    check_non_negative(description.contact_hertz, "the contact stiffness", backend, operation, context);
    check_non_negative(description.contact_damping_ratio, "the contact damping ratio", backend, operation, context);
    check_non_negative(description.maximum_push_speed, "the maximum push speed", backend, operation, context);
}

void
check_description(const SleepDescription& description,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context)
{
    check_non_negative(description.linear_threshold, "the linear sleep threshold", backend, operation, context);
    check_non_negative(description.angular_threshold, "the angular sleep threshold", backend, operation, context);
    check_non_negative(description.time, "the sleep time", backend, operation, context);
}

void
check_world_description(const PhysicsWorldDescription& description,
                        const BackendIdentity& backend)
{
    check_description(description, backend, "create_physics_world", {});
}

void
check_description(const PhysicsWorldDescription& description,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context)
{
    check_vector(description.gravity, "the gravity", backend, operation, context);
    check_description(description.solver, backend, operation, context);
    check_description(description.sleep, backend, operation, context);
    check_non_negative(description.collision.speculative_distance, "the speculative distance", backend, operation, context);
    if (description.collision.maximum_manifolds == 0)
    {
        reject(backend, operation, "the collision settings keep no manifold", "keep at least one manifold per pair", context);
    }
    check_non_negative(description.hit_event_speed, "the hit event speed", backend, operation, context);
}

} // namespace gfx::physics
