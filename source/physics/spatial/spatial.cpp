// The spatial structures' errors, and structures of each kind behind the virtual interface.
module gfx.physics.spatial;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.primitives;

namespace gfx::physics::spatial_detail
{

void
reject_proxy(std::string_view operation,
             SpatialProxyHandle proxy)
{
    throw PhysicsError({.backend = PhysicsSubsystem::SPATIAL,
                        .operation = std::string{operation},
                        .problem = std::format("{} is not a live proxy of this structure (null, removed, or another structure's)", proxy),
                        .suggestion = "use the handle insert() returned, before remove()",
                        .context = {}});
}

void
reject_loose_region(const Aabb& region)
{
    throw PhysicsError({.backend = PhysicsSubsystem::SPATIAL,
                        .operation = "LooseTree::LooseTree",
                        .problem = std::format("the region from ({}, {}, {}) to ({}, {}, {}) has no extent along a split axis", region.minimum.x, region.minimum.y,
                                               region.minimum.z, region.maximum.x, region.maximum.y, region.maximum.z),
                        .suggestion = "give the box the tree subdivides (objects outside it still work, at the root)",
                        .context = {}});
}

void
reject_broad_phase(const BroadPhaseDescription& description)
{
    throw PhysicsError({.backend = PhysicsSubsystem::SPATIAL,
                        .operation = "SpatialBroadPhase::SpatialBroadPhase",
                        .problem = std::format("invalid description: margin {}, prediction {}, proxies per task {}", description.margin, description.prediction,
                                               description.proxies_per_task),
                        .suggestion = "give a finite margin and prediction of at least 0 and at least one proxy per task",
                        .context = {}});
}

void
reject_null_collider(std::string_view operation)
{
    throw PhysicsError({.backend = PhysicsSubsystem::SPATIAL,
                        .operation = std::string{operation},
                        .problem = "the collider handle is null",
                        .suggestion = "give the collider the proxy stands for",
                        .context = {}});
}

} // namespace gfx::physics::spatial_detail

namespace gfx::physics
{

auto
make_spatial_structure(SpatialStructureKind kind) -> std::unique_ptr<SpatialStructure>
{
    switch (kind)
    {
        case SpatialStructureKind::BVH:
            return std::make_unique<SpatialStructureAdapter<Bvh>>();
        case SpatialStructureKind::OCTREE:
            return std::make_unique<SpatialStructureAdapter<Octree<>>>();
        case SpatialStructureKind::QUADTREE:
            return std::make_unique<SpatialStructureAdapter<Quadtree<>>>();
        case SpatialStructureKind::BSP:
            return std::make_unique<SpatialStructureAdapter<Bsp>>();
        case SpatialStructureKind::UNIFORM_GRID:
            return std::make_unique<SpatialStructureAdapter<UniformGrid<>>>();
        case SpatialStructureKind::SWEEP_AND_PRUNE:
            return std::make_unique<SpatialStructureAdapter<SweepAndPrune<>>>();
    }
    throw PhysicsError({.backend = PhysicsSubsystem::SPATIAL,
                        .operation = "make_spatial_structure",
                        .problem = std::format("{} is not a spatial structure kind", std::to_underlying(kind)),
                        .suggestion = "pass an enumerator of SpatialStructureKind",
                        .context = {}});
}

} // namespace gfx::physics
