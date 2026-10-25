/// @file snapshot.cppm
/// @brief What a SteppedWorld knows of its objects after a step: a record per object (its configuration, the state the
///        step left it in), the step's events, contacts and statistics (WorldSnapshot).
///
/// A world keeps two snapshots. The pending one belongs to the step: the framework applies the edits to it, then the
/// backend writes its results into it (body poses and velocities, joint forces, soft-body particles, events, contacts,
/// statistics). When the step is published, the pending snapshot is copied into the published one, which getters read
/// (records whose configuration did not change copy only their state). Records are indexed by the handles' slots;
/// find() gives the record a live handle names.
export module gfx.physics.world:snapshot;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.interface;

export namespace gfx::physics
{

/// @brief A rigid body.
struct RigidBodyRecord
{
    RigidBodyHandle handle{};             ///< Null: a free slot.
    RigidBodyDescription configuration{}; ///< Its pose and velocities are those it was created with (see state).
    MassProperties mass{};                ///< In effect: given, or its colliders' (zero unless dynamic).
    RigidBodyState state{};               ///< Where it is and how it moves.
    bool configuration_changed = false;   ///< Set by the framework when an edit changed the configuration or the mass.
};

/// @brief A collider.
struct ColliderRecord
{
    ColliderHandle handle{};             ///< Null: a free slot.
    RigidBodyHandle body{};              ///< Its body.
    ColliderDescription configuration{}; ///< Its configuration.
    MassProperties unit_mass{};          ///< Its shape's at density 1, in the shape's frame.
    Aabb world_bounds{};                 ///< Its box in world space (computed by the framework from its body's pose).
    bool configuration_changed = false;  ///< Set by the framework when an edit changed the configuration.
};

/// @brief A joint.
struct JointRecord
{
    JointHandle handle{};               ///< Null: a free slot.
    JointDescription configuration{};   ///< Its configuration.
    JointState state{};                 ///< How it did.
    bool configuration_changed = false; ///< Set by the framework when an edit changed the configuration.
};

/// @brief A trigger.
struct TriggerRecord
{
    TriggerHandle handle{};             ///< Null: a free slot.
    TriggerDescription configuration{}; ///< Its configuration.
    Aabb world_bounds{};                ///< Its box in world space (computed by the framework).
    bool configuration_changed = false; ///< Set by the framework when an edit changed the configuration.
};

/// @brief A soft body.
struct SoftBodyRecord
{
    SoftBodyHandle handle{};             ///< Null: a free slot.
    SoftBodyDescription configuration{}; ///< Its configuration (the particles' starting positions).
    std::vector<Vector3> positions{};    ///< Its particles' positions.
    std::vector<Vector3> velocities{};   ///< Its particles' velocities.
    Aabb bounds = Aabb::empty();         ///< The box around its particles.
    bool configuration_changed = false;  ///< Set by the framework when an edit changed the configuration.
};

/// @brief What a world knows after a step.
struct WorldSnapshot
{
    PhysicsWorldDescription configuration{};     ///< The world's settings.
    bool configuration_changed = false;          ///< Set by the framework when an edit changed them.
    std::vector<RigidBodyRecord> rigid_bodies{}; ///< By slot.
    std::vector<ColliderRecord> colliders{};     ///< By slot.
    std::vector<JointRecord> joints{};           ///< By slot.
    std::vector<TriggerRecord> triggers{};       ///< By slot.
    std::vector<SoftBodyRecord> soft_bodies{};   ///< By slot.
    std::vector<ContactEvent> contact_events{};  ///< The step's (cleared when it starts).
    std::vector<TriggerEvent> trigger_events{};  ///< The step's.
    std::vector<JointEvent> joint_events{};      ///< The step's.
    std::vector<SleepEvent> sleep_events{};      ///< The step's.
    std::vector<ContactInfo> contacts{};         ///< Every touching pair after the step, the backend's (cleared when it starts).
    StepStatistics statistics{};                 ///< The step's (counts filled by the framework, phases and pairs by the backend).
};

/// @brief The record @p body names, or null (a null, stale or other world's handle).
[[nodiscard]] auto find(WorldSnapshot& snapshot,
                        RigidBodyHandle body) noexcept -> RigidBodyRecord*;

/// @brief The record @p body names, or null.
[[nodiscard]] auto find(const WorldSnapshot& snapshot,
                        RigidBodyHandle body) noexcept -> const RigidBodyRecord*;

/// @brief The record @p collider names, or null.
[[nodiscard]] auto find(WorldSnapshot& snapshot,
                        ColliderHandle collider) noexcept -> ColliderRecord*;

/// @brief The record @p collider names, or null.
[[nodiscard]] auto find(const WorldSnapshot& snapshot,
                        ColliderHandle collider) noexcept -> const ColliderRecord*;

/// @brief The record @p joint names, or null.
[[nodiscard]] auto find(WorldSnapshot& snapshot,
                        JointHandle joint) noexcept -> JointRecord*;

/// @brief The record @p joint names, or null.
[[nodiscard]] auto find(const WorldSnapshot& snapshot,
                        JointHandle joint) noexcept -> const JointRecord*;

/// @brief The record @p trigger names, or null.
[[nodiscard]] auto find(WorldSnapshot& snapshot,
                        TriggerHandle trigger) noexcept -> TriggerRecord*;

/// @brief The record @p trigger names, or null.
[[nodiscard]] auto find(const WorldSnapshot& snapshot,
                        TriggerHandle trigger) noexcept -> const TriggerRecord*;

/// @brief The record @p soft_body names, or null.
[[nodiscard]] auto find(WorldSnapshot& snapshot,
                        SoftBodyHandle soft_body) noexcept -> SoftBodyRecord*;

/// @brief The record @p soft_body names, or null.
[[nodiscard]] auto find(const WorldSnapshot& snapshot,
                        SoftBodyHandle soft_body) noexcept -> const SoftBodyRecord*;

/// @brief The pose of @p collider's frame in the world (its body's pose times its own), from @p snapshot.
[[nodiscard]] auto world_pose(const WorldSnapshot& snapshot,
                              const ColliderRecord& collider) noexcept -> Transform;

} // namespace gfx::physics
