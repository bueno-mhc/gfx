// The Box3D backend's internals (not exported): Box3dBackend, a SteppedWorldBackend over one Box3D world — the Box3D ids
// of each object, the reverse tables from Box3D's ids to our handles, the touching pairs and trigger overlaps the events
// are made from, and the bridge from Box3D's task hooks to the world's TaskScheduler —, and the conversions between the
// two libraries' values. Box3D's C headers come in this unit's global module fragment.
module;

#include <box3d/box3d.h>

module gfx.physics.backends.box3d:backend;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.world;

namespace gfx::physics::box3d_backend
{

// ===================================================================================================== conversions

[[nodiscard]] inline auto
to_box3d(Vector3 vector) noexcept -> b3Vec3
{
    return {.x = vector.x, .y = vector.y, .z = vector.z};
}

[[nodiscard]] inline auto
from_box3d(b3Vec3 vector) noexcept -> Vector3
{
    return {.x = vector.x, .y = vector.y, .z = vector.z};
}

[[nodiscard]] inline auto
to_box3d(const Quaternion& rotation) noexcept -> b3Quat
{
    return {.v = {.x = rotation.x, .y = rotation.y, .z = rotation.z}, .s = rotation.w};
}

[[nodiscard]] inline auto
from_box3d(b3Quat rotation) noexcept -> Quaternion
{
    return {.x = rotation.v.x, .y = rotation.v.y, .z = rotation.v.z, .w = rotation.s};
}

[[nodiscard]] inline auto
to_box3d(const Transform& transform) noexcept -> b3Transform
{
    return {.p = to_box3d(transform.position), .q = to_box3d(transform.orientation)};
}

[[nodiscard]] inline auto
from_box3d(const b3Transform& transform) noexcept -> Transform
{
    return {.position = from_box3d(transform.p), .orientation = from_box3d(transform.q)};
}

[[nodiscard]] inline auto
to_box3d(const CollisionFilter& filter) noexcept -> b3Filter
{
    return {.categoryBits = std::to_underlying(filter.category), .maskBits = std::to_underlying(filter.mask), .groupIndex = filter.group};
}

/// Equal Box3D ids.
template<class Id>
[[nodiscard]] inline auto
same_id(Id first,
        Id second) noexcept -> bool
{
    return first.index1 == second.index1 && first.world0 == second.world0 && first.generation == second.generation;
}

// ===================================================================================================== the backend

/// Who a Box3D shape belongs to.
struct ShapeOwner
{
    ColliderHandle collider{};              ///< The collider (null for a trigger's sensor).
    TriggerHandle trigger{};                ///< The trigger (null for a collider's shape).
    std::uint32_t child = NO_SUB_SHAPE;     ///< The compound child it comes from.
};

/// Box3D data a shape references (meshes and height fields are not copied by Box3D).
struct GeometryDeleter
{
    void operator()(b3MeshData* mesh) const noexcept
    {
        b3DestroyMesh(mesh);
    }

    void operator()(b3HeightFieldData* field) const noexcept
    {
        b3DestroyHeightField(field);
    }
};

/// The Box3D side of a rigid body.
struct BodyEntry
{
    RigidBodyHandle handle{};
    b3BodyId id{};
    bool mass_changed = false;
};

/// The Box3D side of a collider: one shape per convex part (compounds become several shapes on the body).
struct ColliderEntry
{
    ColliderHandle handle{};
    RigidBodyHandle body{};
    std::vector<b3ShapeId> shapes{};
    std::vector<std::unique_ptr<b3MeshData, GeometryDeleter>> meshes{};
    std::vector<std::unique_ptr<b3HeightFieldData, GeometryDeleter>> height_fields{};
    CollisionFilter filter{};
    bool contact_events = true;
};

/// The Box3D side of a trigger: sensor shapes on its body, or on a static body of its own.
struct TriggerEntry
{
    TriggerHandle handle{};
    std::vector<b3ShapeId> shapes{};
    std::vector<std::unique_ptr<b3MeshData, GeometryDeleter>> meshes{};
    std::vector<std::unique_ptr<b3HeightFieldData, GeometryDeleter>> height_fields{};
    b3BodyId own_body{};
    bool has_own_body = false;
};

/// The Box3D side of a joint, and the frames it was created with (to create it again when re-enabled or updated).
struct JointEntry
{
    JointHandle handle{};
    b3JointId id{};
    bool exists = false;
    bool breakable = false;
    b3Transform frame_first{};
    b3Transform frame_second{};
};

/// Two colliders touching, through one or more pairs of their shapes.
struct TouchingPair
{
    ColliderHandle first{};
    ColliderHandle second{};
    std::vector<b3ContactId> contacts{};
};

/// A collider in a trigger, through one or more shapes.
struct TriggerOverlap
{
    TriggerHandle trigger{};
    ColliderHandle collider{};
    std::uint32_t shapes = 0;
    std::uint64_t entered = 0;
};

/// Box3D's task hooks over the world's TaskScheduler: each task Box3D enqueues during a step is started on the scheduler
/// and its ticket handed back to Box3D (which waits for it through finish).
struct TaskBridge
{
    TaskScheduler* scheduler = nullptr;
    std::array<TaskTicket, B3_MAX_TASKS> tickets{};
    std::atomic<std::uint32_t> used{0};
};

/// A world of Box3D.
class Box3dBackend final : public SteppedWorldBackend, private HandleIssuer
{
public:
    /// @throws BackendCapabilityError if Box3D holds as many worlds as it can.
    explicit Box3dBackend(const PhysicsWorldDescription& description);
    ~Box3dBackend() override;

    Box3dBackend(const Box3dBackend&) = delete;
    auto operator=(const Box3dBackend&) -> Box3dBackend& = delete;

    [[nodiscard]] auto identity() const noexcept -> BackendIdentity override
    {
        return PhysicsBackendKind::BOX3D;
    }

    [[nodiscard]] auto pipeline() const noexcept -> PipelineKinds override
    {
        return {.broad_phase = BroadPhaseKind::BACKEND,
                .narrow_phase = NarrowPhaseKind::BACKEND,
                .solver = SolverKind::SOFT_STEP,
                .integrator = IntegratorKind::SEMI_IMPLICIT_EULER};
    }

    void check(const WorldEdit& edit,
               const RecordedWorld& recorded) const override;

    void apply(std::span<const WorldEdit> edits,
               WorldSnapshot& pending) override;

    void simulate(float seconds,
                  WorldSnapshot& pending) override;

    [[nodiscard]] auto ray_cast(const Ray& ray,
                                float maximum,
                                const QueryOptions& options) const noexcept -> std::optional<QueryResult<RayHit>> override;
    [[nodiscard]] auto ray_cast_all(const Ray& ray,
                                    float maximum,
                                    std::span<RayHit> output,
                                    const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<RayHit>>> override;
    [[nodiscard]] auto shape_cast(const Shape& shape,
                                  const Transform& pose,
                                  Vector3 translation,
                                  const QueryOptions& options) const noexcept -> std::optional<QueryResult<ShapeCastHit>> override;
    [[nodiscard]] auto overlap(const Shape& shape,
                               const Transform& pose,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>> override;
    [[nodiscard]] auto overlap(const Aabb& box,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>> override;

    // ------------------------------------------------------------------------------------------------ shared by the units

    /// The slot of a handle.
    template<class Tag>
    [[nodiscard]] static auto slot(Handle<Tag> handle) noexcept -> std::uint32_t
    {
        return handle.index(handle_key());
    }

    /// The collider a Box3D shape belongs to (null: none, or a sensor).
    [[nodiscard]] auto shape_owner(b3ShapeId shape) const noexcept -> const ShapeOwner*;

    /// The Box3D side of @p collider (null: not a live collider).
    [[nodiscard]] auto collider_entry(ColliderHandle collider) const noexcept -> const ColliderEntry*;

private:
    // box3d_shapes.cpp
    void check_shape(const Shape& shape,
                     const Transform& pose,
                     BodyKind body_kind,
                     std::uint32_t round_sides,
                     std::string_view operation,
                     const PhysicsErrorContext& context) const;
    void create_collider(const edits::CreateCollider& change,
                         const WorldSnapshot& pending);
    void destroy_collider(ColliderHandle collider,
                          WorldSnapshot& pending);
    void create_shapes(const Shape& shape,
                       const Transform& pose,
                       b3BodyId body,
                       const b3ShapeDef& definition,
                       std::uint32_t round_sides,
                       std::uint32_t child,
                       ShapeOwner owner,
                       std::vector<b3ShapeId>& shapes,
                       ColliderEntry* geometry);
    [[nodiscard]] auto shape_definition(const ColliderDescription& description) const -> b3ShapeDef;
    void create_trigger(const edits::CreateTrigger& change,
                        const WorldSnapshot& pending);
    void destroy_trigger(TriggerHandle trigger,
                         WorldSnapshot& pending);

    // box3d_joints.cpp
    void check_joint(const JointDescription& description,
                     std::string_view operation) const;
    void create_joint(JointHandle joint,
                      const JointDescription& description,
                      const WorldSnapshot& pending);
    void make_joint(JointEntry& entry,
                    const JointDescription& description);
    void destroy_joint(JointEntry& entry);

    // box3d_backend.cpp
    void apply_mass(RigidBodyHandle body,
                    WorldSnapshot& pending);
    void capture_bodies(WorldSnapshot& pending);
    void capture_contacts(WorldSnapshot& pending);
    void capture_triggers(WorldSnapshot& pending);
    void capture_joints(WorldSnapshot& pending);
    void end_touches_of(ColliderHandle collider,
                        WorldSnapshot& pending);
    void end_overlaps_of(ColliderHandle collider,
                         TriggerHandle trigger,
                         WorldSnapshot& pending);
    void own_shape(b3ShapeId shape,
                   ShapeOwner owner);
    void disown_shape(b3ShapeId shape);

    // ------------------------------------------------------------------------------------------------ friends (unit-local visitors)
    friend struct EditApplier;

private:
    b3WorldId m_world{};
    b3BodyId m_ground{};
    std::unique_ptr<TaskBridge> m_tasks{};
    int m_sub_steps = 4;
    float m_sleep_threshold = 0.05F;
    std::uint64_t m_step = 0;

    std::vector<BodyEntry> m_bodies{};            ///< By body slot.
    std::vector<ColliderEntry> m_colliders{};     ///< By collider slot.
    std::vector<TriggerEntry> m_triggers{};       ///< By trigger slot.
    std::vector<JointEntry> m_joints{};           ///< By joint slot.
    std::vector<ShapeOwner> m_shape_owners{};     ///< By Box3D shape index.
    std::vector<RigidBodyHandle> m_body_owners{}; ///< By Box3D body index.
    std::vector<JointHandle> m_joint_owners{};    ///< By Box3D joint index.

    std::unordered_map<std::uint64_t, TouchingPair> m_touching{};    ///< By the two colliders' slots.
    std::unordered_map<std::uint64_t, TriggerOverlap> m_overlaps{};  ///< By the trigger's and the collider's slots.
};

} // namespace gfx::physics::box3d_backend
