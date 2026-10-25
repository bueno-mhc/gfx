// collide(): the dispatch over shape kinds (compounds, convex pairs, convex against triangles, triangles against
// triangles), active-edge correction, the merging of manifolds that share a normal, and shape_distance().
module gfx.physics.narrow_phase;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.shapes;

namespace gfx::physics
{

namespace
{

/// Throws the PhysicsError of @p operation.
[[noreturn]] void
reject(std::string_view operation,
       std::string problem,
       std::string_view suggestion)
{
    throw PhysicsError({.backend = PhysicsSubsystem::NARROW_PHASE, .operation = std::string{operation}, .problem = std::move(problem), .suggestion = std::string{suggestion}, .context = {}});
}

/// Calls `visitor(geometry)` with @p shape's convex geometry (nothing for the other kinds).
template<class Visitor>
void
visit_convex(const Shape& shape,
             Visitor&& visitor)
{
    std::visit(
        [&visitor](const auto& geometry) {
            if constexpr (FacedConvexShape<std::remove_cvref_t<decltype(geometry)>>)
            {
                visitor(geometry);
            }
        },
        shape.geometry());
}

/// Calls `visitor(index, triangle, active_edges, two_sided)` for the triangles of the mesh or height field @p shape
/// whose boxes overlap @p box (in the shape's frame).
template<class Visitor>
void
query_triangles(const Shape& shape,
                const Aabb& box,
                Visitor&& visitor)
{
    if (const auto* mesh = shape.get_if<TriangleMeshShape>())
    {
        mesh->query(box, [&](std::uint32_t index, const TriangleShape& triangle) { visitor(index, triangle, mesh->active_edges(index), mesh->two_sided()); });
    }
    else if (const auto* field = shape.get_if<HeightFieldShape>())
    {
        field->query(box, [&](std::uint32_t index, const TriangleShape& triangle) { visitor(index, triangle, field->active_edges(index), false); });
    }
}

/// @p manifold (in the frame of @p pose) moved into the world.
void
to_world(ContactManifold& manifold,
         const Transform& pose)
{
    manifold.normal = rotate(pose.orientation, manifold.normal);
    for (ContactPoint& point : std::span{manifold.points}.first(manifold.point_count))
    {
        point.position = transform_point(pose, point.position);
    }
}

/// Gives @p manifold (a convex shape against @p triangle, in the triangle's frame) the triangle's face normal when every
/// point lies on the face or on inactive edges and vertices: no bump at the seams of flat meshes.
void
correct_inactive_edges(ContactManifold& manifold,
                       const TriangleShape& triangle,
                       std::uint8_t active_edges)
{
    const Vector3 front = triangle.normal();
    const Vector3 face_normal = dot(manifold.normal, front) <= 0.0F ? -front : front;
    if (dot(manifold.normal, face_normal) > 0.9999F)
    {
        return;
    }
    const std::array<Vector3, 3> corners{triangle.first, triangle.second, triangle.third};
    for (const ContactPoint& point : manifold.contacts())
    {
        // The point's barycentric coordinates in the triangle's plane; near 0 means on the opposite edge.
        const Vector3 v0 = corners[1] - corners[0];
        const Vector3 v1 = corners[2] - corners[0];
        const Vector3 v2 = point.position - corners[0];
        const float d00 = dot(v0, v0);
        const float d01 = dot(v0, v1);
        const float d11 = dot(v1, v1);
        const float d20 = dot(v2, v0);
        const float d21 = dot(v2, v1);
        const float denominator = d00 * d11 - d01 * d01;
        if (!(std::abs(denominator) > 0.0F))
        {
            return;
        }
        const float v = (d11 * d20 - d01 * d21) / denominator;
        const float w = (d00 * d21 - d01 * d20) / denominator;
        const std::array<float, 3> weights{1.0F - v - w, v, w};
        constexpr float ON_EDGE = 1e-4F;
        bool on_active = false;
        for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
        {
            // Weight of vertex k near 0: the point is on edge k + 1 (from vertex k + 1 to k + 2).
            if (weights[vertex] <= ON_EDGE)
            {
                const std::uint32_t edge = (vertex + 1) % 3;
                on_active = on_active || (active_edges & (1U << edge)) != 0;
            }
        }
        if (on_active)
        {
            return;
        }
    }
    manifold.normal = face_normal;
}

/// What a collision gathers into, and how.
struct Gatherer
{
    const CollisionSettings& settings;
    std::vector<ContactManifold>& found;

    /// Records @p manifold with its sub-shapes.
    void record(ContactManifold manifold,
                std::uint32_t sub_first,
                std::uint32_t sub_second)
    {
        manifold.sub_shape_first = sub_first;
        manifold.sub_shape_second = sub_second;
        found.push_back(manifold);
    }

    /// Any two shapes; @p sub_first and @p sub_second are the compound children they are (NO_SUB_SHAPE outside compounds).
    void pair(const Shape& first,
              const Transform& first_pose,
              std::uint32_t sub_first,
              const Shape& second,
              const Transform& second_pose,
              std::uint32_t sub_second)
    {
        // Compounds: the children under the partner's box, at their poses.
        if (const auto* compound = first.get_if<CompoundShape>())
        {
            const Aabb box = second.bounds(inverse(first_pose) * second_pose).expanded(settings.speculative_distance);
            compound->query(box, [&](std::uint32_t index, const CompoundChild& child) {
                pair(*child.shape, first_pose * child.pose, sub_first == NO_SUB_SHAPE ? index : sub_first, second, second_pose, sub_second);
            });
            return;
        }
        if (const auto* compound = second.get_if<CompoundShape>())
        {
            const Aabb box = first.bounds(inverse(second_pose) * first_pose).expanded(settings.speculative_distance);
            compound->query(box, [&](std::uint32_t index, const CompoundChild& child) {
                pair(first, first_pose, sub_first, *child.shape, second_pose * child.pose, sub_second == NO_SUB_SHAPE ? index : sub_second);
            });
            return;
        }
        if (first.is_convex() && second.is_convex())
        {
            visit_convex(first, [&](const auto& a) {
                visit_convex(second, [&](const auto& b) {
                    if (const std::optional<ContactManifold> manifold = collide_convex(a, first_pose, b, second_pose, settings))
                    {
                        record(*manifold, sub_first, sub_second);
                    }
                });
            });
        }
        else if (first.is_convex())
        {
            convex_triangles(first, first_pose, sub_first, second, second_pose, sub_second, false);
        }
        else if (second.is_convex())
        {
            convex_triangles(second, second_pose, sub_second, first, first_pose, sub_first, true);
        }
        else
        {
            triangles_triangles(first, first_pose, sub_first, second, second_pose, sub_second);
        }
    }

    /// A convex shape against the triangles of a mesh or height field (in the triangles' frame), flipped back when the
    /// triangles were the first shape.
    void convex_triangles(const Shape& convex,
                          const Transform& convex_pose,
                          std::uint32_t convex_sub,
                          const Shape& soup,
                          const Transform& soup_pose,
                          std::uint32_t soup_sub,
                          bool flip)
    {
        const Transform relative = inverse(soup_pose) * convex_pose;
        const Aabb box = convex.bounds(relative).expanded(settings.speculative_distance);
        visit_convex(convex, [&](const auto& geometry) {
            query_triangles(soup, box, [&](std::uint32_t index, const TriangleShape& triangle, std::uint8_t active_edges, bool two_sided) {
                std::optional<ContactManifold> manifold = collide_convex(geometry, relative, triangle, Transform{}, settings);
                if (!manifold)
                {
                    return;
                }
                // One-sided triangles make no contact from behind (a normal from the convex shape along the front).
                if (!two_sided && dot(manifold->normal, triangle.normal()) > 0.0F)
                {
                    return;
                }
                correct_inactive_edges(*manifold, triangle, active_edges);
                for (ContactPoint& point : std::span{manifold->points}.first(manifold->point_count))
                {
                    point.feature.sub_shape_second = index;
                }
                to_world(*manifold, soup_pose);
                const std::uint32_t triangle_sub = soup_sub == NO_SUB_SHAPE ? index : soup_sub;
                if (flip)
                {
                    record(flipped(*manifold), triangle_sub, convex_sub);
                }
                else
                {
                    record(*manifold, convex_sub, triangle_sub);
                }
            });
        });
    }

    /// Two meshes or height fields: each triangle of the first under the second's box against the second's triangles
    /// under its own box (in the first's frame).
    void triangles_triangles(const Shape& first,
                             const Transform& first_pose,
                             std::uint32_t first_sub,
                             const Shape& second,
                             const Transform& second_pose,
                             std::uint32_t second_sub)
    {
        const Transform relative = inverse(first_pose) * second_pose;
        const Transform back = inverse(relative);
        const Aabb box = second.bounds(relative).expanded(settings.speculative_distance);
        query_triangles(first, box, [&](std::uint32_t first_index, const TriangleShape& first_triangle, std::uint8_t, bool) {
            const Aabb near = first_triangle.bounds().transformed(back).expanded(settings.speculative_distance);
            query_triangles(second, near, [&](std::uint32_t second_index, const TriangleShape& second_local, std::uint8_t, bool) {
                const TriangleShape second_triangle{.first = transform_point(relative, second_local.first),
                                                    .second = transform_point(relative, second_local.second),
                                                    .third = transform_point(relative, second_local.third)};
                std::optional<ContactManifold> manifold = collide_convex(first_triangle, Transform{}, second_triangle, Transform{}, settings);
                if (!manifold)
                {
                    return;
                }
                for (ContactPoint& point : std::span{manifold->points}.first(manifold->point_count))
                {
                    point.feature.sub_shape_first = first_index;
                    point.feature.sub_shape_second = second_index;
                }
                to_world(*manifold, first_pose);
                record(*manifold, first_sub == NO_SUB_SHAPE ? first_index : first_sub, second_sub == NO_SUB_SHAPE ? second_index : second_sub);
            });
        });
    }
};

} // namespace

void
collide(const ShapeInstance& first,
        const ShapeInstance& second,
        ContactCollector& contacts,
        const CollisionSettings& settings)
{
    // Valid input.
    if (first.shape == nullptr || second.shape == nullptr)
    {
        reject("collide", "a shape instance has no shape", "give both instances a shape");
    }
    if (!std::isfinite(settings.speculative_distance) || !(settings.speculative_distance >= 0.0F) || settings.maximum_manifolds == 0
        || !(settings.merge_cosine >= -1.0F && settings.merge_cosine <= 1.0F))
    {
        reject("collide",
               std::format("invalid settings: speculative distance {}, merge cosine {}, maximum manifolds {}", settings.speculative_distance, settings.merge_cosine,
                           settings.maximum_manifolds),
               "give a finite speculative distance of at least 0, a cosine in [-1, 1] and at least one manifold");
    }

    // Every manifold of every sub-shape pair.
    contacts.m_manifolds.clear();
    contacts.m_found.clear();
    Gatherer gatherer{.settings = settings, .found = contacts.m_found};
    gatherer.pair(*first.shape, first.pose, NO_SUB_SHAPE, *second.shape, second.pose, NO_SUB_SHAPE);

    // Those whose normals agree merge (their points reduced to four); the deepest contribution names the sub-shapes.
    for (const ContactManifold& manifold : contacts.m_found)
    {
        ContactManifold* target = nullptr;
        for (ContactManifold& merged : contacts.m_manifolds)
        {
            if (dot(merged.normal, manifold.normal) >= settings.merge_cosine)
            {
                target = &merged;
                break;
            }
        }
        if (target == nullptr)
        {
            contacts.m_manifolds.push_back(manifold);
            continue;
        }
        contacts.m_points.assign(target->points.begin(), target->points.begin() + target->point_count);
        contacts.m_points.insert(contacts.m_points.end(), manifold.points.begin(), manifold.points.begin() + manifold.point_count);
        if (manifold.deepest() < target->deepest())
        {
            target->sub_shape_first = manifold.sub_shape_first;
            target->sub_shape_second = manifold.sub_shape_second;
        }
        reduce_contact_points(contacts.m_points, target->normal, *target);
    }

    // The deepest manifolds, at most the maximum.
    if (contacts.m_manifolds.size() > settings.maximum_manifolds)
    {
        std::ranges::stable_sort(contacts.m_manifolds, std::less{}, [](const ContactManifold& manifold) { return manifold.deepest(); });
        contacts.m_manifolds.resize(settings.maximum_manifolds);
    }
}

auto
shape_distance(const ShapeInstance& first,
               const ShapeInstance& second) -> ShapeDistance
{
    if (first.shape == nullptr || second.shape == nullptr || !first.shape->is_convex() || !second.shape->is_convex())
    {
        reject("shape_distance", "both shapes must be convex (sphere, capsule, cylinder, cone, box or hull)",
               "use collide() for meshes, height fields and compounds");
    }
    ShapeDistance result{};
    visit_convex(*first.shape, [&](const auto& a) {
        visit_convex(*second.shape, [&](const auto& b) { result = convex_distance(a, first.pose, b, second.pose); });
    });
    return result;
}

} // namespace gfx::physics
