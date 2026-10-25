/// @file descriptions.cppm
/// @brief The values of the physics interface: what worlds, rigid bodies, colliders, triggers, joints and soft bodies are
///        made of and the state a step leaves them in, what queries take and give, and what a step reports (events,
///        statistics). Plain aggregates with defaults, for designated initializers.
export module gfx.physics.interface:descriptions;

import std;
import gfx.core.enums;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.tasks;

export namespace gfx::physics
{

// ===================================================================================================== materials

/// @brief Friction coefficients (Coulomb): static while sliding stays under the solver's threshold, dynamic beyond.
struct Friction
{
    float static_coefficient = 0.6F;  ///< At least 0.
    float dynamic_coefficient = 0.6F; ///< At least 0.
    float rolling = 0.0F;             ///< Rolling resistance, at least 0.

    [[nodiscard]] constexpr auto operator==(const Friction&) const noexcept -> bool = default;
};

/// @brief How a collider's surface responds to contact.
struct SurfaceMaterial
{
    Friction friction{};      ///< Friction.
    float restitution = 0.0F; ///< Bounciness in [0, 1] (above the solver's restitution threshold).

    [[nodiscard]] constexpr auto operator==(const SurfaceMaterial&) const noexcept -> bool = default;
};

// ===================================================================================================== worlds

/// @brief How a world's solver works (backends map what they support; the reference solver uses all of it).
struct SolverDescription
{
    std::uint32_t sub_steps = 4;                                 ///< Sub-steps per step (Box3D's soft step), at least 1.
    std::uint32_t velocity_iterations = 8;                       ///< Velocity iterations per sub-step (sequential impulses), at least 1.
    std::uint32_t position_iterations = 3;                       ///< Position-correction iterations (non-linear Gauss–Seidel).
    bool warm_starting = true;                                   ///< Start from last step's impulses.
    float restitution_threshold = DEFAULT_RESTITUTION_THRESHOLD; ///< Approach speed under which contacts do not bounce.
    float static_friction_speed = 0.01F;                         ///< Sliding speed under which static friction applies.
    float contact_hertz = 30.0F;                                 ///< Stiffness of soft contacts.
    float contact_damping_ratio = 10.0F;                         ///< Damping of soft contacts.
    float maximum_push_speed = 3.0F;                             ///< Fastest overlap is resolved at.

    [[nodiscard]] constexpr auto operator==(const SolverDescription&) const noexcept -> bool = default;
};

/// @brief When bodies sleep.
struct SleepDescription
{
    bool enabled = true;             ///< Whether bodies may sleep at all.
    float linear_threshold = 0.05F;  ///< Speed under which a body may sleep.
    float angular_threshold = 0.05F; ///< Angular speed (radians per second) under which a body may sleep.
    float time = 0.5F;               ///< Seconds its island must stay under the thresholds.

    [[nodiscard]] constexpr auto operator==(const SleepDescription&) const noexcept -> bool = default;
};

/// @brief A world.
struct PhysicsWorldDescription
{
    Vector3 gravity{.y = -STANDARD_GRAVITY}; ///< Acceleration of every body (times its gravity scale).
    bool asynchronous_stepping = false;      ///< begin_step() runs the step on the world's own thread.
    TaskScheduler* task_scheduler = nullptr; ///< Splits steps over the application's threads (not owned; outlives the world).
    SolverDescription solver{};              ///< The solver.
    SleepDescription sleep{};                ///< Sleeping.
    CollisionSettings collision{};           ///< Speculative distance, manifold merging.
    float hit_event_speed = 1.0F;            ///< Approach speed from which contacts report HIT events.
    std::string debug_name{};                ///< For errors and tools.
};

// ===================================================================================================== bodies and colliders

/// @brief A rigid body.
struct RigidBodyDescription
{
    BodyKind kind = BodyKind::DYNAMIC;                      ///< Static, kinematic or dynamic.
    Transform pose{};                                       ///< Where it starts (its frame: the colliders' poses are in it).
    Vector3 linear_velocity{};                              ///< Its velocity at the start.
    Vector3 angular_velocity{};                             ///< Its angular velocity (radians per second, world axes).
    float linear_damping = 0.0F;                            ///< Per second, at least 0.
    float angular_damping = 0.05F;                          ///< Per second, at least 0.
    float gravity_scale = 1.0F;                             ///< Times the world's gravity.
    MotionQuality motion_quality = MotionQuality::DISCRETE; ///< Continuous collision for fast bodies.
    MotionLock locks = MotionLock::NONE;                    ///< Axes it may not move along or about.
    std::optional<MassProperties> mass_properties{};        ///< Given; empty: from its colliders' shapes and densities.
    bool allow_sleep = true;                                ///< Whether it may sleep.
    bool awake = true;                                      ///< Whether it starts awake.
    std::uint64_t user_data = 0;                            ///< The application's.
};

/// @brief Where a rigid body is and how it moves.
struct RigidBodyState
{
    Transform pose{};           ///< Its frame in the world.
    Vector3 linear_velocity{};  ///< Of its frame's origin.
    Vector3 angular_velocity{}; ///< Radians per second, world axes.
    bool awake = true;          ///< Whether it is simulated (false: asleep).

    [[nodiscard]] constexpr auto operator==(const RigidBodyState&) const noexcept -> bool = default;
};

/// @brief A collider: a shape on a body.
struct ColliderDescription
{
    std::shared_ptr<const Shape> shape{};    ///< Its geometry (shared; not null).
    Transform pose{};                        ///< In the body's frame.
    SurfaceMaterial material{};              ///< Friction and restitution.
    float density = 1000.0F;                 ///< Mass per unit volume (kilograms per cubic meter by default units), at least 0.
    CollisionFilter filter{};                ///< Layers, mask, group.
    bool contact_events = true;              ///< Whether its contacts make events.
    std::uint32_t round_shape_segments = 16; ///< Cylinders and cones become hulls of so many sides on backends without them.
    std::uint64_t user_data = 0;             ///< The application's.
};

/// @brief A trigger: a volume reporting what enters, stays in and leaves it.
struct TriggerDescription
{
    std::shared_ptr<const Shape> shape{}; ///< Its volume (not null).
    Transform pose{};                     ///< In the world, or in the frame of `body`.
    RigidBodyHandle body{};               ///< The body it follows (null: fixed in the world).
    CollisionFilter filter{};             ///< What it detects.
};

// ===================================================================================================== joints

/// @brief Limits of an axis (meters or radians).
struct JointLimits
{
    float lower = 0.0F; ///< At most upper.
    float upper = 0.0F; ///< At least lower.

    [[nodiscard]] constexpr auto operator==(const JointLimits&) const noexcept -> bool = default;
};

/// @brief A drive on an axis: a target speed reached with at most a force (or torque).
struct JointMotor
{
    float target_speed = 0.0F;  ///< Meters or radians per second.
    float maximum_force = 0.0F; ///< Newtons or newton-meters, at least 0 (0: no drive).

    [[nodiscard]] constexpr auto operator==(const JointMotor&) const noexcept -> bool = default;
};

/// @brief Softness: a spring at a frequency with a damping ratio (frequency 0: rigid).
struct JointSpring
{
    float frequency = 0.0F;     ///< Hertz, at least 0.
    float damping_ratio = 0.0F; ///< At least 0 (1: critical).

    [[nodiscard]] constexpr auto operator==(const JointSpring&) const noexcept -> bool = default;
};

/// @brief When a joint breaks (it is disabled and reports a JointEvent).
struct JointBreaking
{
    float force = std::numeric_limits<float>::infinity();  ///< Newtons; infinite: never.
    float torque = std::numeric_limits<float>::infinity(); ///< Newton-meters; infinite: never.

    [[nodiscard]] constexpr auto operator==(const JointBreaking&) const noexcept -> bool = default;
};

/// @brief Keeps two points of two bodies at a distance (rod), within a range (rope), around it (spring) or together (stub).
struct DistanceJointDescription
{
    RigidBodyHandle first{};                                       ///< A body.
    RigidBodyHandle second{};                                      ///< The other (null: the world).
    Vector3 anchor_first{};                                        ///< In the first body's frame.
    Vector3 anchor_second{};                                       ///< In the second body's frame (the world's when null).
    float length = 1.0F;                                           ///< The rest distance (ROPE: the longest; 0 for STUB), at least 0.
    DistanceMode mode = DistanceMode::ROD;                         ///< How the distance is held.
    float minimum_length = 0.0F;                                   ///< SPRING and ROPE: the shortest allowed.
    float maximum_length = std::numeric_limits<float>::infinity(); ///< SPRING: the longest allowed (a rope's is its length).
    JointSpring spring{};                                          ///< SPRING: its softness.
    JointBreaking breaking{};                                      ///< When it breaks.
    bool collide_connected = false;                                ///< Whether the two bodies still collide.
};

/// @brief One rotation about an axis (a revolute joint).
struct HingeJointDescription
{
    RigidBodyHandle first{};             ///< A body.
    RigidBodyHandle second{};            ///< The other (null: the world).
    Vector3 anchor{};                    ///< World space, at creation.
    Vector3 axis{.y = 1.0F};             ///< World space, at creation (unit).
    std::optional<JointLimits> limits{}; ///< Angles, radians.
    std::optional<JointMotor> motor{};   ///< A drive.
    JointSpring spring{};                ///< Softness toward the creation angle.
    JointBreaking breaking{};            ///< When it breaks.
    bool collide_connected = false;      ///< Whether the two bodies still collide.
};

/// @brief Rotation about a point (a spherical joint).
struct BallAndSocketJointDescription
{
    RigidBodyHandle first{};        ///< A body.
    RigidBodyHandle second{};       ///< The other (null: the world).
    Vector3 anchor{};               ///< World space, at creation.
    JointBreaking breaking{};       ///< When it breaks.
    bool collide_connected = false; ///< Whether the two bodies still collide.
};

/// @brief Translation along an axis (a prismatic joint).
struct SliderJointDescription
{
    RigidBodyHandle first{};             ///< A body.
    RigidBodyHandle second{};            ///< The other (null: the world).
    Vector3 anchor{};                    ///< World space, at creation.
    Vector3 axis{.x = 1.0F};             ///< World space, at creation (unit).
    std::optional<JointLimits> limits{}; ///< Distances along the axis.
    std::optional<JointMotor> motor{};   ///< A drive.
    JointSpring spring{};                ///< Softness toward the creation position.
    JointBreaking breaking{};            ///< When it breaks.
    bool collide_connected = false;      ///< Whether the two bodies still collide.
};

/// @brief No relative motion (a weld), rigid or soft.
struct FixedJointDescription
{
    RigidBodyHandle first{};        ///< A body.
    RigidBodyHandle second{};       ///< The other (null: the world).
    Vector3 anchor{};               ///< World space, at creation.
    JointSpring linear_spring{};    ///< Softness of the position.
    JointSpring angular_spring{};   ///< Softness of the orientation.
    JointBreaking breaking{};       ///< When it breaks.
    bool collide_connected = false; ///< Whether the two bodies still collide.
};

/// @brief A ball and socket with the swing in a cone around an axis and the twist about it limited.
struct ConeTwistJointDescription
{
    RigidBodyHandle first{};                               ///< A body.
    RigidBodyHandle second{};                              ///< The other (null: the world).
    Vector3 anchor{};                                      ///< World space, at creation.
    Vector3 axis{.y = 1.0F};                               ///< The twist axis, world space at creation (unit).
    float swing_limit = std::numbers::pi_v<float> * 0.25F; ///< The cone's half angle, radians, in (0, π).
    std::optional<JointLimits> twist_limits{};             ///< Twist angles, radians.
    JointBreaking breaking{};                              ///< When it breaks.
    bool collide_connected = false;                        ///< Whether the two bodies still collide.
};

/// @brief Each of the three translations and three rotations of a frame free, limited or locked, with drives.
struct SixDegreesOfFreedomJointDescription
{
    RigidBodyHandle first{};                           ///< A body.
    RigidBodyHandle second{};                          ///< The other (null: the world).
    Transform frame{};                                 ///< The joint's axes, world space at creation.
    std::array<AxisMotion, 6> motion{};                ///< Per JointAxis (all free by default).
    std::array<JointLimits, 6> limits{};               ///< Per JointAxis, for LIMITED axes.
    std::array<std::optional<JointMotor>, 6> motors{}; ///< Per JointAxis.
    JointBreaking breaking{};                          ///< When it breaks.
    bool collide_connected = false;                    ///< Whether the two bodies still collide.
};

/// @brief How a joint did in a step.
struct JointState
{
    bool enabled = true; ///< False once broken or disabled.
    bool broken = false; ///< Whether it broke (a JointEvent reported it).
    Vector3 force{};     ///< The constraint force on the second body, world space.
    Vector3 torque{};    ///< The constraint torque on the second body.

    [[nodiscard]] constexpr auto operator==(const JointState&) const noexcept -> bool = default;
};

/// @brief Any joint, in JointKind's order.
using JointDescription = std::variant<DistanceJointDescription, HingeJointDescription, BallAndSocketJointDescription, SliderJointDescription, FixedJointDescription,
                                      ConeTwistJointDescription, SixDegreesOfFreedomJointDescription>;

/// @brief The kind of joint @p description describes.
[[nodiscard]] constexpr auto
joint_kind(const JointDescription& description) noexcept -> JointKind
{
    return static_cast<JointKind>(description.index());
}

/// @brief The two bodies @p description joins (the second null for the world).
[[nodiscard]] constexpr auto
joint_bodies(const JointDescription& description) noexcept -> std::pair<RigidBodyHandle, RigidBodyHandle>
{
    return std::visit([](const auto& joint) { return std::pair{joint.first, joint.second}; }, description);
}

/// @brief A distance joint (for `create_joint(distance_joint({...}))`).
[[nodiscard]] constexpr auto
distance_joint(const DistanceJointDescription& description) -> JointDescription
{
    return description;
}

/// @brief A hinge joint.
[[nodiscard]] constexpr auto
hinge_joint(const HingeJointDescription& description) -> JointDescription
{
    return description;
}

/// @brief A ball-and-socket joint.
[[nodiscard]] constexpr auto
ball_and_socket_joint(const BallAndSocketJointDescription& description) -> JointDescription
{
    return description;
}

/// @brief A slider joint.
[[nodiscard]] constexpr auto
slider_joint(const SliderJointDescription& description) -> JointDescription
{
    return description;
}

/// @brief A fixed joint.
[[nodiscard]] constexpr auto
fixed_joint(const FixedJointDescription& description) -> JointDescription
{
    return description;
}

/// @brief A cone-twist joint.
[[nodiscard]] constexpr auto
cone_twist_joint(const ConeTwistJointDescription& description) -> JointDescription
{
    return description;
}

/// @brief A six-degrees-of-freedom joint.
[[nodiscard]] constexpr auto
six_degrees_of_freedom_joint(const SixDegreesOfFreedomJointDescription& description) -> JointDescription
{
    return description;
}

// ===================================================================================================== soft bodies

/// @brief A soft-body particle.
struct SoftBodyParticle
{
    Vector3 position{};        ///< Where it starts.
    float inverse_mass = 1.0F; ///< 1 / its mass (0: it does not move).
};

/// @brief A constraint between particles (XPBD: its compliance is the inverse of its stiffness; 0: rigid).
struct SoftBodyConstraint
{
    SoftBodyConstraintKind kind = SoftBodyConstraintKind::STRETCH; ///< What it keeps.
    std::array<std::uint32_t, 4> particles{};                      ///< Two for distances, four for bending and volumes.
    float compliance = 0.0F;                                       ///< At least 0.
    float rest_value = -1.0F;                                      ///< Its rest length or volume; negative: from the starting positions.
};

/// @brief A particle held to a point of the world or of a rigid body.
struct ParticlePin
{
    std::uint32_t particle = 0; ///< The particle.
    RigidBodyHandle body{};     ///< The body (null: the world).
    Vector3 point{};            ///< In the body's frame (the world's when null).
    float compliance = 0.0F;    ///< At least 0 (0: held exactly).
};

/// @brief A soft body: particles, constraints between them, pins.
struct SoftBodyDescription
{
    std::vector<SoftBodyParticle> particles{};     ///< At least one.
    std::vector<SoftBodyConstraint> constraints{}; ///< Their particle indices in range.
    std::vector<ParticlePin> pins{};               ///< Particles held.
    float particle_radius = 0.02F;                 ///< For collisions, greater than 0.
    float damping = 0.01F;                         ///< Velocity lost per second, in [0, 1].
    float friction = 0.3F;                         ///< Against colliders, at least 0.
    float gravity_scale = 1.0F;                    ///< Times the world's gravity.
    std::uint32_t sub_steps = 8;                   ///< XPBD sub-steps per step, at least 1.
    bool self_collision = false;                   ///< Whether its particles collide with each other.
    CollisionFilter filter{};                      ///< Against colliders.
};

// ===================================================================================================== queries

/// @brief What a world query considers and who answers it (queries find colliders; triggers report through events).
struct QueryOptions
{
    CollisionFilter filter{.category = ALL_LAYERS, .mask = ALL_LAYERS}; ///< The query as a collider: what it hits.
    QueryEngine engine = QueryEngine::BACKEND;                          ///< The backend's queries or the abstraction's.
};

/// @brief A ray's hit.
struct RayHit
{
    ColliderHandle collider{};              ///< What it hit.
    RigidBodyHandle body{};                 ///< Its body.
    float distance = 0.0F;                  ///< Along the ray.
    Vector3 point{};                        ///< World space.
    Vector3 normal{};                       ///< The surface's, world space.
    std::uint32_t sub_shape = NO_SUB_SHAPE; ///< The triangle or child hit.
};

/// @brief A shape cast's first hit.
struct ShapeCastHit
{
    ColliderHandle collider{};              ///< What it hit.
    RigidBodyHandle body{};                 ///< Its body.
    float fraction = 0.0F;                  ///< Of the translation, in [0, 1] (0: overlapping at the start).
    Vector3 point{};                        ///< The contact on what it hit, world space.
    Vector3 normal{};                       ///< From the cast shape to what it hit.
    std::uint32_t sub_shape = NO_SUB_SHAPE; ///< The triangle or child hit.
};

/// @brief The nearest collider to a point.
struct ClosestPointHit
{
    ColliderHandle collider{};              ///< The nearest collider.
    RigidBodyHandle body{};                 ///< Its body.
    Vector3 point{};                        ///< Its nearest surface point.
    float distance = 0.0F;                  ///< From the point (negative inside).
    std::uint32_t sub_shape = NO_SUB_SHAPE; ///< The triangle or child it is on.
};

/// @brief A body's contact.
struct ContactInfo
{
    ColliderHandle collider{};    ///< The body's collider.
    ColliderHandle other{};       ///< What it touches.
    RigidBodyHandle other_body{}; ///< Its body.
    ContactManifold manifold{};   ///< From `collider` to `other`.
};

// ===================================================================================================== events and statistics

/// @brief Two colliders began, ended or hit.
struct ContactEvent
{
    ContactEventKind kind = ContactEventKind::BEGIN; ///< What happened.
    ColliderHandle first{};                          ///< A collider.
    ColliderHandle second{};                         ///< The other.
    Vector3 point{};                                 ///< World space (BEGIN, HIT).
    Vector3 normal{};                                ///< From first to second.
    float approach_speed = 0.0F;                     ///< HIT: their speed toward each other.
};

/// @brief A collider entered, stays in or left a trigger.
struct TriggerEvent
{
    TriggerEventKind kind = TriggerEventKind::ENTER; ///< What happened.
    TriggerHandle trigger{};                         ///< The trigger.
    ColliderHandle collider{};                       ///< The collider.
};

/// @brief A joint broke.
struct JointEvent
{
    JointEventKind kind = JointEventKind::BROKEN; ///< What happened.
    JointHandle joint{};                          ///< The joint.
    float force = 0.0F;                           ///< Its constraint force.
    float torque = 0.0F;                          ///< Its constraint torque.
};

/// @brief A body fell asleep or woke up.
struct SleepEvent
{
    SleepEventKind kind = SleepEventKind::FELL_ASLEEP; ///< What happened.
    RigidBodyHandle body{};                            ///< The body.
};

/// @brief What the last step reported (valid until the next step completes).
struct PhysicsEvents
{
    std::span<const ContactEvent> contacts{}; ///< Contacts begun, ended, hit.
    std::span<const TriggerEvent> triggers{}; ///< Triggers entered, stayed in, left.
    std::span<const JointEvent> joints{};     ///< Joints broken.
    std::span<const SleepEvent> sleeps{};     ///< Bodies asleep, awake.
};

/// @brief How the last step went.
struct StepStatistics
{
    std::uint64_t step = 0;                                          ///< Steps completed.
    float seconds = 0.0F;                                            ///< Simulated time of the last step.
    std::array<double, ENUMERATOR_COUNT<StepPhase>> phase_seconds{}; ///< Wall time per phase.
    std::uint32_t rigid_bodies = 0;                                  ///< Live rigid bodies.
    std::uint32_t awake_bodies = 0;                                  ///< Of them, awake.
    std::uint32_t soft_bodies = 0;                                   ///< Live soft bodies.
    std::uint32_t joints = 0;                                        ///< Live joints.
    std::uint32_t pairs = 0;                                         ///< Candidate pairs.
    std::uint32_t contacts = 0;                                      ///< Contact points.
    std::uint32_t edits = 0;                                         ///< Edits applied at its start.
};

} // namespace gfx::physics
