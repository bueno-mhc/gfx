// The Box3D backend's own queries (QueryEngine::BACKEND): Box3D's ray casts, shape casts and overlaps, its shapes mapped
// back to colliders (one hit per collider, whatever its parts), the query's filter applied as colliders are (groups
// included), ties to the smaller handle. Shapes Box3D cannot cast exactly (cylinders, cones: hulls in Box3D; meshes,
// height fields, compounds) and closest points are left to the abstraction (std::nullopt). Box3D's queries skip sensors
// (triggers), ignore back sides of meshes and do not report a ray that starts inside a shape.
module;

#include <box3d/box3d.h>

module gfx.physics.backends.box3d;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.world;
import :backend;

namespace gfx::physics::box3d_backend
{

namespace
{

/// The failure of a Box3D query of @p kind.
[[nodiscard]] auto
failure(QueryFailureKind kind,
        std::string_view operation,
        std::string_view detail) noexcept -> QueryFailure
{
    return {.kind = kind, .operation = operation, .detail = detail, .backend = PhysicsBackendKind::BOX3D, .capability = std::nullopt, .context = {}};
}

/// Every shape: the query's own filter is applied as colliders are (with groups) when Box3D reports a shape.
[[nodiscard]] auto
every_shape() noexcept -> b3QueryFilter
{
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.categoryBits = std::numeric_limits<std::uint64_t>::max();
    filter.maskBits = std::numeric_limits<std::uint64_t>::max();
    return filter;
}

/// The length of a ray of @p maximum Box3D casts (infinite: Box3D's largest distance).
[[nodiscard]] auto
ray_length(float maximum) noexcept -> float
{
    return std::isfinite(maximum) ? maximum : B3_HUGE;
}

/// The sub-shape a hit reports: the compound child it is part of, else the triangle.
[[nodiscard]] auto
sub_shape(const ShapeOwner& owner,
          int triangle) noexcept -> std::uint32_t
{
    return owner.child != NO_SUB_SHAPE ? owner.child : triangle >= 0 ? static_cast<std::uint32_t>(triangle) : NO_SUB_SHAPE;
}

/// What the callbacks share.
struct QueryContext
{
    const Box3dBackend& backend;
    const QueryOptions& options;

    /// The collider of @p shape, if the query takes it.
    [[nodiscard]] auto accepted(b3ShapeId shape) const noexcept -> const ColliderEntry*
    {
        const ShapeOwner* owner = backend.shape_owner(shape);
        const ColliderEntry* collider = owner != nullptr && owner->collider ? backend.collider_entry(owner->collider) : nullptr;
        return collider != nullptr && should_collide(options.filter, collider->filter) ? collider : nullptr;
    }
};

// ===================================================================================================== rays

struct ClosestRay : QueryContext
{
    Ray ray{};
    float length = 0.0F;
    std::optional<RayHit> best{};
};

auto
closest_ray(b3ShapeId shape,
            b3Pos point,
            b3Vec3 normal,
            float fraction,
            std::uint64_t /*material*/,
            int triangle,
            int /*child*/,
            void* context) -> float
{
    ClosestRay& query = *static_cast<ClosestRay*>(context);
    const ColliderEntry* collider = query.accepted(shape);
    if (collider == nullptr)
    {
        return -1.0F;
    }
    const float distance = fraction * query.length;
    if (!query.best || distance < query.best->distance || (distance == query.best->distance && collider->handle < query.best->collider))
    {
        query.best = RayHit{.collider = collider->handle,
                            .body = collider->body,
                            .distance = distance,
                            .point = from_box3d(point),
                            .normal = from_box3d(normal),
                            .sub_shape = sub_shape(*query.backend.shape_owner(shape), triangle)};
    }
    return fraction;
}

struct EveryRay : QueryContext
{
    Ray ray{};
    float length = 0.0F;
    std::span<RayHit> output{};
    std::size_t count = 0;
    bool exceeded = false;
};

auto
every_ray(b3ShapeId shape,
          b3Pos point,
          b3Vec3 normal,
          float fraction,
          std::uint64_t /*material*/,
          int triangle,
          int /*child*/,
          void* context) -> float
{
    // One hit per collider, the nearest kept in order (nearest first, then the smaller handle).
    EveryRay& query = *static_cast<EveryRay*>(context);
    const ColliderEntry* collider = query.accepted(shape);
    if (collider == nullptr)
    {
        return 1.0F;
    }
    const RayHit hit{.collider = collider->handle,
                     .body = collider->body,
                     .distance = fraction * query.length,
                     .point = from_box3d(point),
                     .normal = from_box3d(normal),
                     .sub_shape = sub_shape(*query.backend.shape_owner(shape), triangle)};
    const auto nearer = [](const RayHit& first, const RayHit& second) {
        return first.distance < second.distance || (first.distance == second.distance && first.collider < second.collider);
    };
    const std::span<RayHit> held = query.output.first(query.count);
    const auto same = std::ranges::find(held, hit.collider, &RayHit::collider);
    if (same != held.end())
    {
        if (!nearer(hit, *same))
        {
            return 1.0F;
        }
        std::shift_left(same, held.end(), 1);
        --query.count;
    }
    if (query.count == query.output.size())
    {
        query.exceeded = true;
        if (query.output.empty() || !nearer(hit, query.output.back()))
        {
            return 1.0F;
        }
        --query.count;
    }
    std::size_t index = query.count++;
    for (; index > 0 && nearer(hit, query.output[index - 1]); --index)
    {
        query.output[index] = query.output[index - 1];
    }
    query.output[index] = hit;
    return 1.0F;
}

// ===================================================================================================== shapes

/// A convex shape as Box3D casts it: points about the pose's position (in world axes) and a radius; nothing for the
/// kinds Box3D does not hold exactly.
struct Proxy
{
    std::array<b3Vec3, B3_MAX_SHAPE_CAST_POINTS> points{};
    int count = 0;
    float radius = 0.0F;

    [[nodiscard]] auto view() const noexcept -> b3ShapeProxy
    {
        return {.points = points.data(), .count = count, .radius = radius};
    }
};

[[nodiscard]] auto
make_proxy(const Shape& shape,
           const Transform& pose) noexcept -> std::optional<Proxy>
{
    Proxy proxy{};
    const auto add = [&](Vector3 local) { proxy.points[static_cast<std::size_t>(proxy.count++)] = to_box3d(rotate(pose.orientation, local)); };
    if (const auto* sphere = shape.get_if<SphereShape>())
    {
        add({});
        proxy.radius = sphere->radius;
    }
    else if (const auto* capsule = shape.get_if<CapsuleShape>())
    {
        add({.y = -capsule->half_height});
        add({.y = capsule->half_height});
        proxy.radius = capsule->radius;
    }
    else if (const auto* box = shape.get_if<BoxShape>())
    {
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            add(Aabb::from_center({}, box->half_extents).corner(corner));
        }
        proxy.radius = box->radius;
    }
    else if (const auto* hull = shape.get_if<HullShape>())
    {
        if (hull->view().vertices.size() > B3_MAX_SHAPE_CAST_POINTS)
        {
            return std::nullopt;
        }
        for (const Vector3 vertex : hull->view().vertices)
        {
            add(vertex);
        }
        proxy.radius = hull->margin();
    }
    else
    {
        return std::nullopt;
    }
    return proxy;
}

struct FirstCast : QueryContext
{
    std::optional<ShapeCastHit> best{};
};

auto
first_cast(b3ShapeId shape,
           b3Pos point,
           b3Vec3 normal,
           float fraction,
           std::uint64_t /*material*/,
           int triangle,
           int /*child*/,
           void* context) -> float
{
    FirstCast& query = *static_cast<FirstCast*>(context);
    const ColliderEntry* collider = query.accepted(shape);
    if (collider == nullptr)
    {
        return -1.0F;
    }
    if (!query.best || fraction < query.best->fraction || (fraction == query.best->fraction && collider->handle < query.best->collider))
    {
        // Box3D's normal is the surface's; ours points from the cast shape to what it hit.
        query.best = ShapeCastHit{.collider = collider->handle,
                                  .body = collider->body,
                                  .fraction = fraction,
                                  .point = from_box3d(point),
                                  .normal = -from_box3d(normal),
                                  .sub_shape = sub_shape(*query.backend.shape_owner(shape), triangle)};
    }
    return fraction;
}

struct Overlapping : QueryContext
{
    std::span<ColliderHandle> output{};
    std::size_t count = 0;
    bool exceeded = false;
    std::optional<b3AABB> box{};
};

auto
overlapping(b3ShapeId shape,
            void* context) -> bool
{
    // Once per collider, the smallest handles kept in order (an AABB query keeps the shapes whose boxes overlap it).
    Overlapping& query = *static_cast<Overlapping*>(context);
    const ColliderEntry* collider = query.accepted(shape);
    if (collider == nullptr)
    {
        return true;
    }
    if (query.box)
    {
        const b3AABB bounds = b3Shape_GetAABB(shape);
        if (bounds.upperBound.x < query.box->lowerBound.x || bounds.lowerBound.x > query.box->upperBound.x || bounds.upperBound.y < query.box->lowerBound.y
            || bounds.lowerBound.y > query.box->upperBound.y || bounds.upperBound.z < query.box->lowerBound.z || bounds.lowerBound.z > query.box->upperBound.z)
        {
            return true;
        }
    }
    const std::span<ColliderHandle> held = query.output.first(query.count);
    if (std::ranges::find(held, collider->handle) != held.end())
    {
        return true;
    }
    if (query.count == query.output.size())
    {
        query.exceeded = true;
        if (query.output.empty() || !(collider->handle < query.output.back()))
        {
            return true;
        }
        --query.count;
    }
    std::size_t index = query.count++;
    for (; index > 0 && collider->handle < query.output[index - 1]; --index)
    {
        query.output[index] = query.output[index - 1];
    }
    query.output[index] = collider->handle;
    return true;
}

} // namespace

auto
Box3dBackend::collider_entry(ColliderHandle collider) const noexcept -> const ColliderEntry*
{
    const std::uint32_t index = slot(collider);
    return index < m_colliders.size() && m_colliders[index].handle == collider ? &m_colliders[index] : nullptr;
}

auto
Box3dBackend::ray_cast(const Ray& ray,
                       float maximum,
                       const QueryOptions& options) const noexcept -> std::optional<QueryResult<RayHit>>
{
    ClosestRay query{{.backend = *this, .options = options}, ray, ray_length(maximum), std::nullopt};
    b3World_CastRay(m_world, to_box3d(ray.origin), to_box3d(ray.direction * query.length), every_shape(), &closest_ray, &query);
    if (!query.best)
    {
        return QueryResult<RayHit>{std::unexpected(failure(QueryFailureKind::NO_HIT, "PhysicsWorld::ray_cast", {}))};
    }
    return QueryResult<RayHit>{*query.best};
}

auto
Box3dBackend::ray_cast_all(const Ray& ray,
                           float maximum,
                           std::span<RayHit> output,
                           const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<RayHit>>>
{
    EveryRay query{{.backend = *this, .options = options}, ray, ray_length(maximum), output, 0, false};
    b3World_CastRay(m_world, to_box3d(ray.origin), to_box3d(ray.direction * query.length), every_shape(), &every_ray, &query);
    if (query.exceeded)
    {
        return QueryResult<std::span<RayHit>>{
            std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, "PhysicsWorld::ray_cast_all", "more hits than the output holds (it holds the nearest)"))};
    }
    return QueryResult<std::span<RayHit>>{output.first(query.count)};
}

auto
Box3dBackend::shape_cast(const Shape& shape,
                         const Transform& pose,
                         Vector3 translation,
                         const QueryOptions& options) const noexcept -> std::optional<QueryResult<ShapeCastHit>>
{
    const std::optional<Proxy> proxy = make_proxy(shape, pose);
    if (!proxy)
    {
        return std::nullopt;
    }
    FirstCast query{{.backend = *this, .options = options}, std::nullopt};
    const b3ShapeProxy view = proxy->view();
    b3World_CastShape(m_world, to_box3d(pose.position), &view, to_box3d(translation), every_shape(), &first_cast, &query);
    if (!query.best)
    {
        return QueryResult<ShapeCastHit>{std::unexpected(failure(QueryFailureKind::NO_HIT, "PhysicsWorld::shape_cast", {}))};
    }
    return QueryResult<ShapeCastHit>{*query.best};
}

auto
Box3dBackend::overlap(const Shape& shape,
                      const Transform& pose,
                      std::span<ColliderHandle> output,
                      const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>
{
    const std::optional<Proxy> proxy = make_proxy(shape, pose);
    if (!proxy)
    {
        return std::nullopt;
    }
    Overlapping query{{.backend = *this, .options = options}, output, 0, false, std::nullopt};
    const b3ShapeProxy view = proxy->view();
    b3World_OverlapShape(m_world, to_box3d(pose.position), &view, every_shape(), &overlapping, &query);
    if (query.exceeded)
    {
        return QueryResult<std::span<ColliderHandle>>{
            std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, "PhysicsWorld::overlap", "more colliders than the output holds"))};
    }
    return QueryResult<std::span<ColliderHandle>>{output.first(query.count)};
}

auto
Box3dBackend::overlap(const Aabb& box,
                      std::span<ColliderHandle> output,
                      const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>
{
    const b3AABB bounds{.lowerBound = to_box3d(box.minimum), .upperBound = to_box3d(box.maximum)};
    Overlapping query{{.backend = *this, .options = options}, output, 0, false, bounds};
    b3World_OverlapAABB(m_world, bounds, every_shape(), &overlapping, &query);
    if (query.exceeded)
    {
        return QueryResult<std::span<ColliderHandle>>{
            std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, "PhysicsWorld::overlap", "more colliders than the output holds"))};
    }
    return QueryResult<std::span<ColliderHandle>>{output.first(query.count)};
}

} // namespace gfx::physics::box3d_backend
