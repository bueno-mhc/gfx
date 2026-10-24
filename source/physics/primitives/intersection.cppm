/// @file intersection.cppm
/// @brief Tests between primitives: classification against planes and frustums, ray distances, segment overlaps.
///        (Overlap tests of two primitives of a kind live with them: intersects(Aabb, Aabb), intersects(Obb, Obb), ...)
export module gfx.physics.primitives:intersection;

import std;
import gfx.core.math;
import gfx.physics.enums;
import :plane;
import :aabb;
import :sphere;
import :obb;
import :ray;
import :segment;
import :convex_hull;
import :frustum;

export namespace gfx::physics
{

// ===================================================================================================== planes

/// @brief Where @p box is with respect to @p plane.
[[nodiscard]] constexpr auto
classify(const Plane& plane,
         const Aabb& box) noexcept -> PlaneSide
{
    const float center = plane.signed_distance(box.center());
    const float reach = dot(component_absolute(plane.normal), box.half_extents());
    return center > reach ? PlaneSide::FRONT : center < -reach ? PlaneSide::BACK : PlaneSide::INTERSECTING;
}

/// @brief Where @p sphere is with respect to @p plane.
[[nodiscard]] constexpr auto
classify(const Plane& plane,
         const Sphere& sphere) noexcept -> PlaneSide
{
    const float center = plane.signed_distance(sphere.center);
    return center > sphere.radius ? PlaneSide::FRONT : center < -sphere.radius ? PlaneSide::BACK : PlaneSide::INTERSECTING;
}

/// @brief Where @p box is with respect to @p plane.
[[nodiscard]] constexpr auto
classify(const Plane& plane,
         const Obb& box) noexcept -> PlaneSide
{
    const std::array<Vector3, 3> axes = box.axes();
    const float reach = std::abs(dot(plane.normal, axes[0])) * box.half_extents.x + std::abs(dot(plane.normal, axes[1])) * box.half_extents.y
                        + std::abs(dot(plane.normal, axes[2])) * box.half_extents.z;
    const float center = plane.signed_distance(box.center);
    return center > reach ? PlaneSide::FRONT : center < -reach ? PlaneSide::BACK : PlaneSide::INTERSECTING;
}

// ===================================================================================================== frustums

/// @brief Where @p box is with respect to @p frustum: OUTSIDE when entirely behind a plane, INSIDE when entirely in
///        front of all six, INTERSECTING otherwise (a box near a corner may be reported INTERSECTING although outside:
///        the usual conservative test). The same arithmetic, in the same order, as the batch classify().
[[nodiscard]] constexpr auto
classify(const Frustum& frustum,
         const Aabb& box) noexcept -> FrustumIntersection
{
    const Vector3 center = box.center();
    const Vector3 extents = box.half_extents();
    bool inside = true;
    for (const Plane& plane : frustum.planes)
    {
        const float distance = dot(plane.normal, center) - plane.distance;
        const float reach = dot(component_absolute(plane.normal), extents);
        if (distance < -reach)
        {
            return FrustumIntersection::OUTSIDE;
        }
        inside = inside && distance >= reach;
    }
    return inside ? FrustumIntersection::INSIDE : FrustumIntersection::INTERSECTING;
}

/// @brief Where @p sphere is with respect to @p frustum (conservative near corners, as for boxes).
[[nodiscard]] constexpr auto
classify(const Frustum& frustum,
         const Sphere& sphere) noexcept -> FrustumIntersection
{
    bool inside = true;
    for (const Plane& plane : frustum.planes)
    {
        const float distance = dot(plane.normal, sphere.center) - plane.distance;
        if (distance < -sphere.radius)
        {
            return FrustumIntersection::OUTSIDE;
        }
        inside = inside && distance >= sphere.radius;
    }
    return inside ? FrustumIntersection::INSIDE : FrustumIntersection::INTERSECTING;
}

/// @brief Where @p box is with respect to @p frustum (conservative near corners, as for boxes).
[[nodiscard]] constexpr auto
classify(const Frustum& frustum,
         const Obb& box) noexcept -> FrustumIntersection
{
    bool inside = true;
    for (const Plane& plane : frustum.planes)
    {
        const PlaneSide side = classify(plane, box);
        if (side == PlaneSide::BACK)
        {
            return FrustumIntersection::OUTSIDE;
        }
        inside = inside && side == PlaneSide::FRONT;
    }
    return inside ? FrustumIntersection::INSIDE : FrustumIntersection::INTERSECTING;
}

// ===================================================================================================== rays

namespace primitives_detail
{

/// The part [entry, exit] of the parameter range [0, @p maximum] of `origin + direction * t` inside @p box (slabs),
/// or nothing. Directions parallel to an axis (within GEOMETRIC_TOLERANCE) keep the slab when the origin is in it.
constexpr auto
slab_interval(Vector3 origin,
              Vector3 direction,
              const Aabb& box,
              float maximum) noexcept -> std::optional<std::pair<float, float>>
{
    float entry = 0;
    float exit = maximum;
    const std::array<float, 3> start{origin.x, origin.y, origin.z};
    const std::array<float, 3> along{direction.x, direction.y, direction.z};
    const std::array<float, 3> low{box.minimum.x, box.minimum.y, box.minimum.z};
    const std::array<float, 3> high{box.maximum.x, box.maximum.y, box.maximum.z};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (std::abs(along[axis]) < GEOMETRIC_TOLERANCE)
        {
            if (start[axis] < low[axis] || start[axis] > high[axis])
            {
                return std::nullopt;
            }
            continue;
        }
        const float first = (low[axis] - start[axis]) / along[axis];
        const float second = (high[axis] - start[axis]) / along[axis];
        entry = std::max(entry, std::min(first, second));
        exit = std::min(exit, std::max(first, second));
        if (entry > exit)
        {
            return std::nullopt;
        }
    }
    return std::pair{entry, exit};
}

} // namespace primitives_detail

/// @brief The distance along @p ray to where it enters @p box (0 when it starts inside), if within @p maximum.
[[nodiscard]] constexpr auto
ray_distance(const Ray& ray,
             const Aabb& box,
             float maximum = std::numeric_limits<float>::infinity()) noexcept -> std::optional<float>
{
    const auto interval = primitives_detail::slab_interval(ray.origin, ray.direction, box, maximum);
    return interval ? std::optional{interval->first} : std::nullopt;
}

/// @brief The distance along @p ray to where it enters @p sphere (0 when it starts inside), if within @p maximum.
[[nodiscard]] constexpr auto
ray_distance(const Ray& ray,
             const Sphere& sphere,
             float maximum = std::numeric_limits<float>::infinity()) noexcept -> std::optional<float>
{
    // |m + t d|² = r² with m = origin - center: t² + 2 b t + c = 0.
    const Vector3 offset = ray.origin - sphere.center;
    const float b = dot(offset, ray.direction);
    const float c = dot(offset, offset) - sphere.radius * sphere.radius;
    if (c > 0 && b > 0)
    {
        return std::nullopt;
    }
    const float discriminant = b * b - c;
    if (discriminant < 0)
    {
        return std::nullopt;
    }
    const float distance = std::max(0.0F, -b - square_root(discriminant));
    return distance <= maximum ? std::optional{distance} : std::nullopt;
}

/// @brief The distance along @p ray to where it enters @p box (0 when it starts inside), if within @p maximum.
[[nodiscard]] constexpr auto
ray_distance(const Ray& ray,
             const Obb& box,
             float maximum = std::numeric_limits<float>::infinity()) noexcept -> std::optional<float>
{
    // The ray in the box's frame, against the axis-aligned box of its extents (rotations keep distances).
    const Quaternion inverse_orientation = conjugate(box.orientation);
    const Ray local{.origin = rotate(inverse_orientation, ray.origin - box.center), .direction = rotate(inverse_orientation, ray.direction)};
    return ray_distance(local, Aabb::from_center({}, box.half_extents), maximum);
}

/// @brief The distance along @p ray to where it crosses @p plane (from either side), if within @p maximum.
[[nodiscard]] constexpr auto
ray_distance(const Ray& ray,
             const Plane& plane,
             float maximum = std::numeric_limits<float>::infinity()) noexcept -> std::optional<float>
{
    const float approach = dot(plane.normal, ray.direction);
    if (std::abs(approach) < GEOMETRIC_TOLERANCE)
    {
        return std::nullopt;
    }
    const float distance = -plane.signed_distance(ray.origin) / approach;
    return distance >= 0 && distance <= maximum ? std::optional{distance} : std::nullopt;
}

/// @brief The distance along @p ray to where it enters @p hull (0 when it starts inside), if within @p maximum
///        (Cyrus-Beck clipping against the faces).
[[nodiscard]] constexpr auto
ray_distance(const Ray& ray,
             const ConvexHullView& hull,
             float maximum = std::numeric_limits<float>::infinity()) noexcept -> std::optional<float>
{
    float entry = 0;
    float exit = maximum;
    for (const Plane& face : hull.faces)
    {
        // Faces face out: the ray enters through faces it approaches and leaves through the others.
        const float approach = dot(face.normal, ray.direction);
        const float height = face.signed_distance(ray.origin);
        if (std::abs(approach) < GEOMETRIC_TOLERANCE)
        {
            if (height > 0)
            {
                return std::nullopt;
            }
            continue;
        }
        const float crossing = -height / approach;
        if (approach < 0)
        {
            entry = std::max(entry, crossing);
        }
        else
        {
            exit = std::min(exit, crossing);
        }
        if (entry > exit)
        {
            return std::nullopt;
        }
    }
    return hull.faces.empty() ? std::nullopt : std::optional{entry};
}

// ===================================================================================================== segments

/// @brief Whether @p segment touches @p box.
[[nodiscard]] constexpr auto
intersects(const Segment& segment,
           const Aabb& box) noexcept -> bool
{
    return primitives_detail::slab_interval(segment.start, segment.direction(), box, 1.0F).has_value();
}

/// @brief Whether @p segment touches @p sphere.
[[nodiscard]] constexpr auto
intersects(const Segment& segment,
           const Sphere& sphere) noexcept -> bool
{
    return segment.squared_distance(sphere.center) <= sphere.radius * sphere.radius;
}

} // namespace gfx::physics
