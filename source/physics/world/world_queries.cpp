// SteppedWorld's queries: the reader handshake (refused while a step runs), the backend's own queries when asked for and
// it has them, else the abstraction's — the query tree over the published colliders' boxes, then the narrow phase's
// queries against each candidate. Nothing allocates and nothing throws; ties between equally near colliders go to the
// smaller handle, so results do not depend on the tree's shape.
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

/// The failure of a query of @p kind.
[[nodiscard]] auto
failure(QueryFailureKind kind,
        std::string_view operation,
        std::string_view detail,
        const BackendIdentity& backend,
        WorldHandle world) noexcept -> QueryFailure
{
    return {.kind = kind, .operation = operation, .detail = detail, .backend = backend, .capability = std::nullopt, .context = {.world = world}};
}

/// Keeps the @p output.size() smallest values seen (by @p less) in order, @p count of them held.
template<class Value, class Less>
void
keep_smallest(std::span<Value> output,
              std::size_t& count,
              const Value& value,
              Less&& less) noexcept
{
    if (count == output.size() && (output.empty() || !less(value, output.back())))
    {
        return;
    }
    std::size_t index = count < output.size() ? count++ : output.size() - 1;
    for (; index > 0 && less(value, output[index - 1]); --index)
    {
        output[index] = output[index - 1];
    }
    output[index] = value;
}

/// What the abstraction's queries read.
struct QueryWorld
{
    const WorldSnapshot& published;
    const Bvh& tree;
    const CollisionFilter& filter;

    /// The collider @p key names, if the query's filter accepts it.
    [[nodiscard]] auto accepted(ColliderHandle key) const noexcept -> const ColliderRecord*
    {
        const ColliderRecord* collider = find(published, key);
        return collider != nullptr && should_collide(filter, collider->configuration.filter) ? collider : nullptr;
    }
};

} // namespace

auto
SteppedWorld::ray_cast(const Ray& ray,
                       float maximum,
                       const QueryOptions& options) const noexcept -> QueryResult<RayHit>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::ray_cast";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    if (options.engine == QueryEngine::BACKEND)
    {
        if (std::optional<QueryResult<RayHit>> answer = m_implementation->backend->ray_cast(ray, maximum, options))
        {
            return *answer;
        }
    }

    // The nearest hit among the colliders whose boxes the ray enters before it.
    const QueryWorld world{.published = m_shared->published, .tree = m_implementation->query_tree, .filter = options.filter};
    std::optional<RayHit> best;
    float nearest = maximum;
    world.tree.query(ray, maximum, [&](ColliderHandle key, float entry) {
        const ColliderRecord* collider = entry <= nearest ? world.accepted(key) : nullptr;
        if (collider == nullptr)
        {
            return true;
        }
        const std::optional<ShapeRayHit> hit = ray_cast_shape(*collider->configuration.shape, world_pose(world.published, *collider), ray, nearest);
        if (hit && (!best || hit->distance < nearest || (hit->distance == nearest && key < best->collider)))
        {
            nearest = hit->distance;
            best = RayHit{.collider = key,
                          .body = collider->body,
                          .distance = hit->distance,
                          .point = ray.at(hit->distance),
                          .normal = hit->normal,
                          .sub_shape = hit->sub_shape};
        }
        return true;
    });
    if (!best)
    {
        return std::unexpected(failure(QueryFailureKind::NO_HIT, OPERATION, {}, backend(), handle()));
    }
    return *best;
}

auto
SteppedWorld::ray_cast_all(const Ray& ray,
                           float maximum,
                           std::span<RayHit> output,
                           const QueryOptions& options) const noexcept -> QueryResult<std::span<RayHit>>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::ray_cast_all";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    if (options.engine == QueryEngine::BACKEND)
    {
        if (std::optional<QueryResult<std::span<RayHit>>> answer = m_implementation->backend->ray_cast_all(ray, maximum, output, options))
        {
            return *answer;
        }
    }

    // Every hit, the nearest kept in order.
    const QueryWorld world{.published = m_shared->published, .tree = m_implementation->query_tree, .filter = options.filter};
    const auto nearer = [](const RayHit& first, const RayHit& second) {
        return first.distance < second.distance || (first.distance == second.distance && first.collider < second.collider);
    };
    std::size_t count = 0;
    bool exceeded = false;
    world.tree.query(ray, maximum, [&](ColliderHandle key, float /*entry*/) {
        if (const ColliderRecord* collider = world.accepted(key))
        {
            if (const std::optional<ShapeRayHit> hit = ray_cast_shape(*collider->configuration.shape, world_pose(world.published, *collider), ray, maximum))
            {
                const RayHit found{.collider = key,
                                   .body = collider->body,
                                   .distance = hit->distance,
                                   .point = ray.at(hit->distance),
                                   .normal = hit->normal,
                                   .sub_shape = hit->sub_shape};
                exceeded = (count == output.size()) || exceeded;
                keep_smallest(output, count, found, nearer);
            }
        }
        return true;
    });
    if (exceeded)
    {
        return std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, OPERATION, "more hits than the output holds (it holds the nearest)", backend(), handle()));
    }
    return output.first(count);
}

auto
SteppedWorld::shape_cast(const Shape& shape,
                         const Transform& pose,
                         Vector3 translation,
                         const QueryOptions& options) const noexcept -> QueryResult<ShapeCastHit>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::shape_cast";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    if (!shape.is_convex())
    {
        return std::unexpected(failure(QueryFailureKind::UNSUPPORTED, OPERATION, "only convex shapes are cast", backend(), handle()));
    }
    if (options.engine == QueryEngine::BACKEND)
    {
        if (std::optional<QueryResult<ShapeCastHit>> answer = m_implementation->backend->shape_cast(shape, pose, translation, options))
        {
            return *answer;
        }
    }

    // The first touch among the colliders whose boxes the swept box overlaps.
    const QueryWorld world{.published = m_shared->published, .tree = m_implementation->query_tree, .filter = options.filter};
    const Aabb start = shape.bounds(pose);
    const Aabb swept = start.merged(Aabb{.minimum = start.minimum + translation, .maximum = start.maximum + translation});
    std::optional<ShapeCastHit> best;
    world.tree.query(swept, [&](ColliderHandle key) {
        if (const ColliderRecord* collider = world.accepted(key))
        {
            const std::optional<ShapeCastResult> hit = cast_shape(shape, pose, translation, *collider->configuration.shape, world_pose(world.published, *collider));
            if (hit && (!best || hit->fraction < best->fraction || (hit->fraction == best->fraction && key < best->collider)))
            {
                best = ShapeCastHit{.collider = key,
                                    .body = collider->body,
                                    .fraction = hit->fraction,
                                    .point = hit->point,
                                    .normal = hit->normal,
                                    .sub_shape = hit->sub_shape};
            }
        }
        return true;
    });
    if (!best)
    {
        return std::unexpected(failure(QueryFailureKind::NO_HIT, OPERATION, {}, backend(), handle()));
    }
    return *best;
}

auto
SteppedWorld::overlap(const Shape& shape,
                      const Transform& pose,
                      std::span<ColliderHandle> output,
                      const QueryOptions& options) const noexcept -> QueryResult<std::span<ColliderHandle>>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::overlap";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    if (!shape.is_convex())
    {
        return std::unexpected(failure(QueryFailureKind::UNSUPPORTED, OPERATION, "only convex shapes are tested for overlaps", backend(), handle()));
    }
    if (options.engine == QueryEngine::BACKEND)
    {
        if (std::optional<QueryResult<std::span<ColliderHandle>>> answer = m_implementation->backend->overlap(shape, pose, output, options))
        {
            return *answer;
        }
    }

    // The colliders the shape touches, among those whose boxes overlap its box; the smallest handles kept in order.
    const QueryWorld world{.published = m_shared->published, .tree = m_implementation->query_tree, .filter = options.filter};
    std::size_t count = 0;
    bool exceeded = false;
    world.tree.query(shape.bounds(pose), [&](ColliderHandle key) {
        const ColliderRecord* collider = world.accepted(key);
        if (collider != nullptr && shapes_overlap(shape, pose, *collider->configuration.shape, world_pose(world.published, *collider)))
        {
            exceeded = (count == output.size()) || exceeded;
            keep_smallest(output, count, key, std::less{});
        }
        return true;
    });
    if (exceeded)
    {
        return std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, OPERATION, "more colliders than the output holds", backend(), handle()));
    }
    return output.first(count);
}

auto
SteppedWorld::overlap(const Aabb& box,
                      std::span<ColliderHandle> output,
                      const QueryOptions& options) const noexcept -> QueryResult<std::span<ColliderHandle>>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::overlap";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    if (options.engine == QueryEngine::BACKEND)
    {
        if (std::optional<QueryResult<std::span<ColliderHandle>>> answer = m_implementation->backend->overlap(box, output, options))
        {
            return *answer;
        }
    }

    // The colliders whose boxes overlap the box.
    const QueryWorld world{.published = m_shared->published, .tree = m_implementation->query_tree, .filter = options.filter};
    std::size_t count = 0;
    bool exceeded = false;
    world.tree.query(box, [&](ColliderHandle key) {
        if (world.accepted(key) != nullptr)
        {
            exceeded = (count == output.size()) || exceeded;
            keep_smallest(output, count, key, std::less{});
        }
        return true;
    });
    if (exceeded)
    {
        return std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, OPERATION, "more colliders than the output holds", backend(), handle()));
    }
    return output.first(count);
}

auto
SteppedWorld::closest_point(Vector3 point,
                            float maximum,
                            const QueryOptions& options) const noexcept -> QueryResult<ClosestPointHit>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::closest_point";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    if (options.engine == QueryEngine::BACKEND)
    {
        if (std::optional<QueryResult<ClosestPointHit>> answer = m_implementation->backend->closest_point(point, maximum, options))
        {
            return *answer;
        }
    }

    // Each candidate whose box is nearer than the best so far.
    const QueryWorld world{.published = m_shared->published, .tree = m_implementation->query_tree, .filter = options.filter};
    std::optional<ClosestPointHit> best;
    float nearest = maximum;
    const auto consider = [&](ColliderHandle key) {
        const ColliderRecord* collider = world.accepted(key);
        if (collider == nullptr || collider->world_bounds.squared_distance(point) > nearest * nearest)
        {
            return;
        }
        const std::optional<ShapeClosestPoint> found = closest_point_on_shape(*collider->configuration.shape, world_pose(world.published, *collider), point, nearest);
        if (found && (!best || found->distance < best->distance || (found->distance == best->distance && key < best->collider)))
        {
            nearest = found->distance;
            best = ClosestPointHit{.collider = key, .body = collider->body, .point = found->point, .distance = found->distance, .sub_shape = found->sub_shape};
        }
    };
    if (std::isfinite(maximum))
    {
        world.tree.query(Sphere{.center = point, .radius = maximum}, [&](ColliderHandle key) {
            consider(key);
            return true;
        });
    }
    else
    {
        world.tree.iterate([&](ColliderHandle key, const Aabb& /*box*/) { consider(key); });
    }
    if (!best)
    {
        return std::unexpected(failure(QueryFailureKind::NO_HIT, OPERATION, {}, backend(), handle()));
    }
    return *best;
}

auto
SteppedWorld::contacts_of(RigidBodyHandle body,
                          std::span<ContactInfo> output) const noexcept -> QueryResult<std::span<ContactInfo>>
{
    constexpr std::string_view OPERATION = "PhysicsWorld::contacts_of";
    const QueryScope scope{*m_implementation};
    if (!scope.active())
    {
        return std::unexpected(failure(QueryFailureKind::STEP_IN_PROGRESS, OPERATION, "a step is running", backend(), handle()));
    }
    const WorldSnapshot& published = m_shared->published;
    if (find(published, body) == nullptr)
    {
        QueryFailure invalid = failure(QueryFailureKind::INVALID_HANDLE, OPERATION, "the body is not in the last completed step", backend(), handle());
        invalid.context.rigid_body = body;
        return std::unexpected(invalid);
    }

    // The contacts of the body's colliders, from the body's side.
    std::size_t count = 0;
    bool exceeded = false;
    const auto body_of = [&published](ColliderHandle collider) {
        const ColliderRecord* record = find(published, collider);
        return record != nullptr ? record->body : RigidBodyHandle{};
    };
    for (const ContactInfo& contact : published.contacts)
    {
        std::optional<ContactInfo> own;
        if (body_of(contact.collider) == body)
        {
            own = contact;
        }
        else if (body_of(contact.other) == body)
        {
            own = ContactInfo{.collider = contact.other, .other = contact.collider, .other_body = body_of(contact.collider), .manifold = flipped(contact.manifold)};
        }
        if (own)
        {
            if (count == output.size())
            {
                exceeded = true;
                continue;
            }
            output[count++] = *own;
        }
    }
    if (exceeded)
    {
        return std::unexpected(failure(QueryFailureKind::CAPACITY_EXCEEDED, OPERATION, "more contacts than the output holds", backend(), handle()));
    }
    return output.first(count);
}

} // namespace gfx::physics
