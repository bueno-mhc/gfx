// The snapshot's lookups: a record by the slot its handle names, if the slot holds that very handle.
module gfx.physics.world;

import std;
import gfx.core.math;
import gfx.physics.handles;
import gfx.physics.interface;
import :state;

namespace gfx::physics
{

namespace
{

/// The record of @p records that @p handle names, or null.
template<class Record, class Tag>
[[nodiscard]] auto
record_of(std::vector<Record>& records,
          Handle<Tag> handle) noexcept -> Record*
{
    if (!handle)
    {
        return nullptr;
    }
    const std::uint32_t slot = handle.index(WorldShared::key());
    return slot < records.size() && records[slot].handle == handle ? &records[slot] : nullptr;
}

/// The record of @p records that @p handle names, or null.
template<class Record, class Tag>
[[nodiscard]] auto
record_of(const std::vector<Record>& records,
          Handle<Tag> handle) noexcept -> const Record*
{
    if (!handle)
    {
        return nullptr;
    }
    const std::uint32_t slot = handle.index(WorldShared::key());
    return slot < records.size() && records[slot].handle == handle ? &records[slot] : nullptr;
}

} // namespace

auto
find(WorldSnapshot& snapshot,
     RigidBodyHandle body) noexcept -> RigidBodyRecord*
{
    return record_of(snapshot.rigid_bodies, body);
}

auto
find(const WorldSnapshot& snapshot,
     RigidBodyHandle body) noexcept -> const RigidBodyRecord*
{
    return record_of(snapshot.rigid_bodies, body);
}

auto
find(WorldSnapshot& snapshot,
     ColliderHandle collider) noexcept -> ColliderRecord*
{
    return record_of(snapshot.colliders, collider);
}

auto
find(const WorldSnapshot& snapshot,
     ColliderHandle collider) noexcept -> const ColliderRecord*
{
    return record_of(snapshot.colliders, collider);
}

auto
find(WorldSnapshot& snapshot,
     JointHandle joint) noexcept -> JointRecord*
{
    return record_of(snapshot.joints, joint);
}

auto
find(const WorldSnapshot& snapshot,
     JointHandle joint) noexcept -> const JointRecord*
{
    return record_of(snapshot.joints, joint);
}

auto
find(WorldSnapshot& snapshot,
     TriggerHandle trigger) noexcept -> TriggerRecord*
{
    return record_of(snapshot.triggers, trigger);
}

auto
find(const WorldSnapshot& snapshot,
     TriggerHandle trigger) noexcept -> const TriggerRecord*
{
    return record_of(snapshot.triggers, trigger);
}

auto
find(WorldSnapshot& snapshot,
     SoftBodyHandle soft_body) noexcept -> SoftBodyRecord*
{
    return record_of(snapshot.soft_bodies, soft_body);
}

auto
find(const WorldSnapshot& snapshot,
     SoftBodyHandle soft_body) noexcept -> const SoftBodyRecord*
{
    return record_of(snapshot.soft_bodies, soft_body);
}

auto
world_pose(const WorldSnapshot& snapshot,
           const ColliderRecord& collider) noexcept -> Transform
{
    const RigidBodyRecord* body = find(snapshot, collider.body);
    return body != nullptr ? body->state.pose * collider.configuration.pose : collider.configuration.pose;
}

} // namespace gfx::physics
