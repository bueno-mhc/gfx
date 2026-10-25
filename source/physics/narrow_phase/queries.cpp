// Queries against one shape: ray casts, casts of convex shapes, overlap tests and closest points — conservative
// advancement over GJK distances for convex shapes, exact tests for spheres and triangles, and the triangles or children
// of meshes, height fields and compounds through their hierarchies.
module gfx.physics.narrow_phase;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import gfx.physics.shapes;

namespace gfx::physics
{

namespace
{

/// Advancement steps before a query gives up (a grazing ray converges slowly).
constexpr std::uint32_t MAXIMUM_ADVANCEMENT_STEPS = 64;

/// Gap under which conservative advancement counts as touching: each step stops half of it short of the plane it aims at,
/// so the distances GJK returns stay positive and their normals exact.
constexpr float TOUCHING_DISTANCE = 1.0e-4F;

/// Gap within GJK's accuracy (curved support functions such as a cylinder's rim): a normal that turns away there may be
/// noise, which a probe PROBE_REACH further along the motion (in distance toward the surface) tells from a graze.
constexpr float NEAR_DISTANCE = 16.0F * TOUCHING_DISTANCE;
constexpr float PROBE_REACH = 4.0F * NEAR_DISTANCE;

/// A point: a convex shape with no extent (the tip of a ray, the query of a closest point).
struct PointShape
{
    [[nodiscard]] constexpr auto support(Vector3 /*direction*/) const noexcept -> Vector3
    {
        return {};
    }

    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0.0F;
    }

    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return {};
    }
};

/// Calls `visitor(geometry)` with @p shape's convex geometry and returns its result, or @p otherwise for the other kinds.
template<class Result, class Visitor>
[[nodiscard]] auto
visit_convex(const Shape& shape,
             Result otherwise,
             Visitor&& visitor) noexcept -> Result
{
    return std::visit(
        [&](const auto& geometry) -> Result {
            if constexpr (ConvexShape<std::remove_cvref_t<decltype(geometry)>>)
            {
                return visitor(geometry);
            }
            else
            {
                return otherwise;
            }
        },
        shape.geometry());
}

/// @p ray in the frame @p to_local maps world space to.
[[nodiscard]] auto
local_ray(const Transform& to_local,
          const Ray& ray) noexcept -> Ray
{
    return {.origin = transform_point(to_local, ray.origin), .direction = rotate(to_local.orientation, ray.direction)};
}

// ===================================================================================================== ray casts

/// The first hit of @p ray on the convex @p shape at @p pose within @p maximum (conservative advancement).
template<ConvexShape Convex>
[[nodiscard]] auto
convex_ray_cast(const Convex& shape,
                const Transform& pose,
                const Ray& ray,
                float maximum) noexcept -> std::optional<ShapeRayHit>
{
    float distance = 0.0F;
    Vector3 normal = -ray.direction;
    float last_approach = 1.0F;
    for (std::uint32_t step = 0; step < MAXIMUM_ADVANCEMENT_STEPS; ++step)
    {
        // The gap from the ray's tip to the shape; inside at the start: a hit at 0.
        const ShapeDistance gap = convex_distance(PointShape{}, Transform{.position = ray.at(distance)}, shape, pose);
        const float approach = dot(gap.normal, ray.direction);
        if (gap.distance < 0.0F && step == 0)
        {
            return ShapeRayHit{.distance = 0.0F, .normal = -ray.direction};
        }

        // Touching (after advancing; at the start, only when going in): the hit is where the ray meets the plane through
        // the closest point. So close, GJK's normal may point anywhere: the last reliable one stands.
        if (gap.distance <= TOUCHING_DISTANCE && (step > 0 || approach > GEOMETRIC_TOLERANCE))
        {
            const bool reliable = approach > GEOMETRIC_TOLERANCE;
            const float hit = distance + (reliable ? std::max(gap.distance, 0.0F) / approach : 0.0F);
            return hit <= maximum ? std::optional{ShapeRayHit{.distance = hit, .normal = reliable ? -gap.normal : normal}} : std::nullopt;
        }

        // Moving away from (or along) the plane through the closest point: no hit — unless the ray came within GJK's accuracy
        // of the surface, where a normal that turned away may be noise: a point a little further along, inside, tells. Otherwise
        // advance to just short of the plane.
        if (approach <= GEOMETRIC_TOLERANCE)
        {
            const float hit = distance + std::max(gap.distance, 0.0F) / last_approach;
            const float probe = distance + PROBE_REACH / last_approach;
            const bool inside_further = step > 0 && gap.distance <= NEAR_DISTANCE && hit <= maximum
                                        && convex_distance(PointShape{}, Transform{.position = ray.at(probe)}, shape, pose).distance <= 0.0F;
            return inside_further ? std::optional{ShapeRayHit{.distance = hit, .normal = normal}} : std::nullopt;
        }
        normal = -gap.normal;
        last_approach = approach;
        distance += (gap.distance - 0.5F * TOUCHING_DISTANCE) / approach;
        if (distance > maximum)
        {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

/// The first hit of @p ray on the sphere of @p radius at @p center within @p maximum (exact).
[[nodiscard]] auto
sphere_ray_cast(Vector3 center,
                float radius,
                const Ray& ray,
                float maximum) noexcept -> std::optional<ShapeRayHit>
{
    const std::optional<float> distance = ray_distance(ray, Sphere{.center = center, .radius = radius}, maximum);
    if (!distance)
    {
        return std::nullopt;
    }
    const Vector3 outward = ray.at(*distance) - center;
    const bool inside = *distance == 0.0F && length_squared(ray.origin - center) < radius * radius;
    return ShapeRayHit{.distance = *distance, .normal = inside || length_squared(outward) == 0.0F ? -ray.direction : normalized(outward)};
}

/// Where @p ray crosses @p triangle within @p maximum (Möller–Trumbore), and the face's normal against the ray; a
/// one-sided triangle is hit from the front only.
[[nodiscard]] auto
triangle_ray_cast(const Ray& ray,
                  const TriangleShape& triangle,
                  float maximum,
                  bool two_sided) noexcept -> std::optional<std::pair<float, Vector3>>
{
    // The determinant is positive when the ray meets the front (it is −dot(direction, normal) times the area).
    const Vector3 first_edge = triangle.second - triangle.first;
    const Vector3 second_edge = triangle.third - triangle.first;
    const Vector3 across = cross(ray.direction, second_edge);
    const float determinant = dot(first_edge, across);
    if (std::abs(determinant) <= 1e-12F * length(first_edge) * length(second_edge) || (!two_sided && determinant < 0.0F))
    {
        return std::nullopt;
    }

    // Barycentric coordinates, then the distance.
    const float inverse_determinant = 1.0F / determinant;
    const Vector3 offset = ray.origin - triangle.first;
    const float u = dot(offset, across) * inverse_determinant;
    if (u < 0.0F || u > 1.0F)
    {
        return std::nullopt;
    }
    const Vector3 lifted = cross(offset, first_edge);
    const float v = dot(ray.direction, lifted) * inverse_determinant;
    if (v < 0.0F || u + v > 1.0F)
    {
        return std::nullopt;
    }
    const float distance = dot(second_edge, lifted) * inverse_determinant;
    if (distance < 0.0F || distance > maximum)
    {
        return std::nullopt;
    }
    const Vector3 normal = triangle.normal();
    return std::pair{distance, determinant > 0.0F ? normal : -normal};
}

/// The first hit of the local ray @p ray on @p mesh within @p maximum, in the mesh's frame.
[[nodiscard]] auto
mesh_ray_cast(const TriangleMeshShape& mesh,
              const Ray& ray,
              float maximum) noexcept -> std::optional<ShapeRayHit>
{
    // Clipped to the mesh's box first, so the segment the hierarchy walks is finite.
    const auto interval = primitives_detail::slab_interval(ray.origin, ray.direction, mesh.bounds(), maximum);
    if (!interval)
    {
        return std::nullopt;
    }
    std::optional<ShapeRayHit> best;
    float nearest = interval->second;
    mesh.bvh().query_segment(ray.origin, ray.direction, interval->second, [&](std::uint32_t index) {
        if (const auto hit = triangle_ray_cast(ray, mesh.triangle(index), nearest, mesh.two_sided()); hit && (!best || hit->first < nearest))
        {
            nearest = hit->first;
            best = ShapeRayHit{.distance = hit->first, .normal = hit->second, .sub_shape = index};
        }
    });
    return best;
}

/// The first hit of the local ray @p ray on @p field within @p maximum, in the field's frame: the cells under pieces of
/// the ray, in order, each piece's hits accepted only within it (a later piece cannot hold a nearer one).
[[nodiscard]] auto
height_field_ray_cast(const HeightFieldShape& field,
                      const Ray& ray,
                      float maximum) noexcept -> std::optional<ShapeRayHit>
{
    const auto interval = primitives_detail::slab_interval(ray.origin, ray.direction, field.bounds().expanded(GEOMETRIC_TOLERANCE), maximum);
    if (!interval)
    {
        return std::nullopt;
    }
    const float piece = 4.0F * std::min(field.scale().x, field.scale().z);
    for (std::uint32_t index = 0;; ++index)
    {
        const float start = interval->first + piece * static_cast<float>(index);
        if (start > interval->second)
        {
            return std::nullopt;
        }
        const float end = std::min(start + piece, interval->second);
        const Aabb box = Aabb{.minimum = ray.at(start), .maximum = ray.at(start)}.merged(ray.at(end)).expanded(GEOMETRIC_TOLERANCE);
        std::optional<ShapeRayHit> best;
        float nearest = end + GEOMETRIC_TOLERANCE;
        field.query(box, [&](std::uint32_t triangle_index, const TriangleShape& triangle) {
            if (const auto hit = triangle_ray_cast(ray, triangle, nearest, false); hit && (!best || hit->first < nearest))
            {
                nearest = hit->first;
                best = ShapeRayHit{.distance = hit->first, .normal = hit->second, .sub_shape = triangle_index};
            }
        });
        if (best)
        {
            best->distance = std::max(best->distance, interval->first);
            return best;
        }
    }
}

/// The first hit of the local ray @p ray on @p compound within @p maximum, in the compound's frame.
[[nodiscard]] auto
compound_ray_cast(const CompoundShape& compound,
                  const Ray& ray,
                  float maximum) noexcept -> std::optional<ShapeRayHit>
{
    const auto interval = primitives_detail::slab_interval(ray.origin, ray.direction, compound.bounds(), maximum);
    if (!interval)
    {
        return std::nullopt;
    }
    std::optional<ShapeRayHit> best;
    float nearest = interval->second;
    compound.bvh().query_segment(ray.origin, ray.direction, interval->second, [&](std::uint32_t index) {
        const CompoundChild& child = compound.children()[index];
        if (const auto hit = ray_cast_shape(*child.shape, child.pose, ray, nearest); hit && (!best || hit->distance < nearest))
        {
            nearest = hit->distance;
            best = ShapeRayHit{.distance = hit->distance, .normal = hit->normal, .sub_shape = index};
        }
    });
    return best;
}

// ===================================================================================================== shape casts

/// Where @p moving at @p pose, moved by @p translation, first touches @p target at @p target_pose, within
/// @p maximum_fraction (conservative advancement).
template<ConvexShape Moving, ConvexShape Target>
[[nodiscard]] auto
convex_cast(const Moving& moving,
            const Transform& pose,
            Vector3 translation,
            const Target& target,
            const Transform& target_pose,
            float maximum_fraction) noexcept -> std::optional<ShapeCastResult>
{
    const float tolerance = GEOMETRIC_TOLERANCE * std::max(1.0F, length(translation));
    float fraction = 0.0F;
    Vector3 normal = normalized(translation);
    float last_approach = 1.0F;
    for (std::uint32_t step = 0; step < MAXIMUM_ADVANCEMENT_STEPS; ++step)
    {
        // The gap at this point of the motion; overlapping at the start: a hit at 0.
        const Transform at{.position = pose.position + translation * fraction, .orientation = pose.orientation};
        const ShapeDistance gap = convex_distance(moving, at, target, target_pose);
        const float approach = dot(translation, gap.normal);
        if (gap.distance < 0.0F && step == 0)
        {
            return ShapeCastResult{.fraction = 0.0F, .point = gap.point_second, .normal = gap.normal};
        }

        // Touching (after advancing; at the start, only when approaching): the hit is where the shapes meet the separating
        // plane. So close, GJK's normal may point anywhere: the last reliable one stands.
        if (gap.distance <= TOUCHING_DISTANCE && (step > 0 || approach > tolerance))
        {
            const bool reliable = approach > tolerance;
            const float hit = fraction + (reliable ? std::max(gap.distance, 0.0F) / approach : 0.0F);
            return hit <= maximum_fraction ? std::optional{ShapeCastResult{.fraction = hit, .point = gap.point_second, .normal = reliable ? gap.normal : normal}}
                                           : std::nullopt;
        }

        // Moving away from (or along) the separating plane: no hit — unless the shapes came within GJK's accuracy of each other,
        // where a normal that turned away may be noise: an overlap a little further along tells. Otherwise advance to just
        // short of the plane.
        if (approach <= tolerance)
        {
            const float hit = fraction + std::max(gap.distance, 0.0F) / last_approach;
            const Transform probe{.position = pose.position + translation * (fraction + PROBE_REACH / last_approach), .orientation = pose.orientation};
            const bool overlap_further = step > 0 && gap.distance <= NEAR_DISTANCE && hit <= maximum_fraction
                                         && convex_distance(moving, probe, target, target_pose).distance <= 0.0F;
            return overlap_further ? std::optional{ShapeCastResult{.fraction = hit, .point = gap.point_second, .normal = normal}} : std::nullopt;
        }
        normal = gap.normal;
        last_approach = approach;
        fraction += (gap.distance - 0.5F * TOUCHING_DISTANCE) / approach;
        if (fraction > maximum_fraction)
        {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

/// The convex @p moving cast in the frame of a mesh, height field or compound (@p local_pose and @p local_translation
/// in that frame) against the triangles or children its swept box overlaps; the hit in that frame.
template<ConvexShape Moving>
[[nodiscard]] auto
cast_against_parts(const Shape& moving_shape,
                   const Moving& moving,
                   const Transform& local_pose,
                   Vector3 local_translation,
                   const Shape& target) noexcept -> std::optional<ShapeCastResult>
{
    const Aabb start = convex_bounds(moving, local_pose);
    const Aabb swept = start.merged(Aabb{.minimum = start.minimum + local_translation, .maximum = start.maximum + local_translation});
    std::optional<ShapeCastResult> best;
    float nearest = 1.0F;
    const auto keep = [&](std::optional<ShapeCastResult> hit, std::uint32_t part) {
        if (hit && (!best || hit->fraction < nearest))
        {
            nearest = hit->fraction;
            best = hit;
            best->sub_shape = part;
        }
    };
    const auto triangles = [&](const TriangleShape& triangle, bool two_sided, std::uint32_t index) {
        // A one-sided triangle stops only what comes at its front.
        if (two_sided || dot(triangle.normal(), local_translation) < 0.0F)
        {
            keep(convex_cast(moving, local_pose, local_translation, triangle, Transform{}, nearest), index);
        }
    };
    if (const TriangleMeshShape* mesh = target.get_if<TriangleMeshShape>())
    {
        mesh->query(swept, [&](std::uint32_t index, const TriangleShape& triangle) { triangles(triangle, mesh->two_sided(), index); });
    }
    else if (const HeightFieldShape* field = target.get_if<HeightFieldShape>())
    {
        field->query(swept, [&](std::uint32_t index, const TriangleShape& triangle) { triangles(triangle, false, index); });
    }
    else if (const CompoundShape* compound = target.get_if<CompoundShape>())
    {
        compound->query(swept, [&](std::uint32_t index, const CompoundChild& child) {
            keep(cast_shape(moving_shape, local_pose, local_translation, *child.shape, child.pose), index);
        });
    }
    return best;
}

// ===================================================================================================== closest points

/// The point of the triangles or children of @p shape (a mesh, height field or compound) nearest to the local point
/// @p point within @p maximum, in the shape's frame.
[[nodiscard]] auto
closest_point_of_parts(const Shape& shape,
                       Vector3 point,
                       float maximum) noexcept -> std::optional<ShapeClosestPoint>
{
    const Aabb box = std::isfinite(maximum) ? Aabb::from_center(point, {.x = maximum, .y = maximum, .z = maximum}) : shape.bounds();
    std::optional<ShapeClosestPoint> best;
    float nearest = maximum;
    const auto keep = [&](float distance, Vector3 on, std::uint32_t part) {
        if (distance <= nearest && (!best || distance < best->distance))
        {
            nearest = distance;
            best = ShapeClosestPoint{.distance = distance, .point = on, .sub_shape = part};
        }
    };
    const auto triangles = [&](std::uint32_t index, const TriangleShape& triangle) {
        const ShapeDistance gap = convex_distance(PointShape{}, Transform{.position = point}, triangle, Transform{});
        keep(std::max(gap.distance, 0.0F), gap.point_second, index);
    };
    if (const TriangleMeshShape* mesh = shape.get_if<TriangleMeshShape>())
    {
        mesh->query(box, triangles);
    }
    else if (const HeightFieldShape* field = shape.get_if<HeightFieldShape>())
    {
        field->query(box, triangles);
    }
    else if (const CompoundShape* compound = shape.get_if<CompoundShape>())
    {
        compound->query(box, [&](std::uint32_t index, const CompoundChild& child) {
            if (const auto found = closest_point_on_shape(*child.shape, child.pose, point, nearest))
            {
                keep(found->distance, found->point, index);
            }
        });
    }
    return best;
}

} // namespace

// ===================================================================================================== the queries

auto
ray_cast_shape(const Shape& shape,
               const Transform& pose,
               const Ray& ray,
               float maximum) noexcept -> std::optional<ShapeRayHit>
{
    // Convex shapes in world space (spheres exactly); the others in their own frame, the normal turned back.
    if (const SphereShape* sphere = shape.get_if<SphereShape>())
    {
        return sphere_ray_cast(pose.position, sphere->radius, ray, maximum);
    }
    if (shape.is_convex())
    {
        return visit_convex(shape, std::optional<ShapeRayHit>{}, [&](const auto& convex) { return convex_ray_cast(convex, pose, ray, maximum); });
    }
    const Ray local = local_ray(inverse(pose), ray);
    std::optional<ShapeRayHit> hit;
    if (const TriangleMeshShape* mesh = shape.get_if<TriangleMeshShape>())
    {
        hit = mesh_ray_cast(*mesh, local, maximum);
    }
    else if (const HeightFieldShape* field = shape.get_if<HeightFieldShape>())
    {
        hit = height_field_ray_cast(*field, local, maximum);
    }
    else if (const CompoundShape* compound = shape.get_if<CompoundShape>())
    {
        hit = compound_ray_cast(*compound, local, maximum);
    }
    if (hit)
    {
        hit->normal = rotate(pose.orientation, hit->normal);
    }
    return hit;
}

auto
cast_shape(const Shape& moving,
           const Transform& pose,
           Vector3 translation,
           const Shape& target,
           const Transform& target_pose) noexcept -> std::optional<ShapeCastResult>
{
    return visit_convex(moving, std::optional<ShapeCastResult>{}, [&](const auto& convex) -> std::optional<ShapeCastResult> {
        // A convex target directly; the others in their own frame, the hit turned back.
        if (target.is_convex())
        {
            return visit_convex(target, std::optional<ShapeCastResult>{},
                                [&](const auto& other) { return convex_cast(convex, pose, translation, other, target_pose, 1.0F); });
        }
        const Transform to_local = inverse(target_pose);
        std::optional<ShapeCastResult> hit = cast_against_parts(moving, convex, to_local * pose, rotate(to_local.orientation, translation), target);
        if (hit)
        {
            hit->point = transform_point(target_pose, hit->point);
            hit->normal = rotate(target_pose.orientation, hit->normal);
        }
        return hit;
    });
}

auto
shapes_overlap(const Shape& first,
               const Transform& pose,
               const Shape& second,
               const Transform& second_pose) noexcept -> bool
{
    return visit_convex(first, false, [&](const auto& convex) -> bool {
        // A convex partner directly; the others' triangles (both sides) or children under the convex shape's box.
        if (second.is_convex())
        {
            return visit_convex(second, false, [&](const auto& other) { return convex_distance(convex, pose, other, second_pose).distance <= 0.0F; });
        }
        const Transform local_pose = inverse(second_pose) * pose;
        const Aabb box = convex_bounds(convex, local_pose);
        bool found = false;
        const auto triangles = [&](std::uint32_t /*index*/, const TriangleShape& triangle) {
            found = found || convex_distance(convex, local_pose, triangle, Transform{}).distance <= 0.0F;
        };
        if (const TriangleMeshShape* mesh = second.get_if<TriangleMeshShape>())
        {
            mesh->query(box, triangles);
        }
        else if (const HeightFieldShape* field = second.get_if<HeightFieldShape>())
        {
            field->query(box, triangles);
        }
        else if (const CompoundShape* compound = second.get_if<CompoundShape>())
        {
            compound->query(box, [&](std::uint32_t /*index*/, const CompoundChild& child) {
                found = found || shapes_overlap(first, local_pose, *child.shape, child.pose);
            });
        }
        return found;
    });
}

auto
closest_point_on_shape(const Shape& shape,
                       const Transform& pose,
                       Vector3 point,
                       float maximum) noexcept -> std::optional<ShapeClosestPoint>
{
    // A convex shape: GJK from the point (EPA inside); the others in their own frame, the point turned back.
    if (shape.is_convex())
    {
        return visit_convex(shape, std::optional<ShapeClosestPoint>{}, [&](const auto& convex) -> std::optional<ShapeClosestPoint> {
            const ShapeDistance gap = convex_distance(PointShape{}, Transform{.position = point}, convex, pose);
            return gap.distance <= maximum ? std::optional{ShapeClosestPoint{.distance = gap.distance, .point = gap.point_second}} : std::nullopt;
        });
    }
    std::optional<ShapeClosestPoint> found = closest_point_of_parts(shape, transform_point(inverse(pose), point), maximum);
    if (found)
    {
        found->point = transform_point(pose, found->point);
    }
    return found;
}

} // namespace gfx::physics
