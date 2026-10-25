/// @file convex.cppm
/// @brief Collision of two convex shapes at their poses, chosen at compile time from their types: the analytic pairs
///        (either order), the separating axis test for boxes and triangles, and for every other pair GJK on the cores
///        (shallow contacts: their distance against the margins) then EPA when the cores overlap, the manifold from
///        face clipping. Also the distance between two convex shapes. constexpr throughout.
export module gfx.physics.narrow_phase:convex;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import gfx.physics.shapes;
import :contact;
import :gjk;
import :epa;
import :clipping;
import :analytic;

export namespace gfx::physics
{

/// @brief The distance between two convex shapes (their surfaces, margins included).
struct ShapeDistance
{
    float distance = 0.0F;     ///< Between the surfaces; negative: the penetration depth.
    Vector3 point_first{};     ///< On the first surface (the deepest point when they overlap).
    Vector3 point_second{};    ///< On the second surface.
    Vector3 normal{.y = 1.0F}; ///< From the first shape to the second.
};

namespace convex_detail
{

/// @p manifold for the shapes in the other order.
[[nodiscard]] constexpr auto
reversed(const std::optional<ContactManifold>& manifold) noexcept -> std::optional<ContactManifold>
{
    return manifold ? std::optional{flipped(*manifold)} : manifold;
}

} // namespace convex_detail

/// @brief The general convex path (GJK, EPA, face clipping), whatever the shapes.
template<FacedConvexShape First, FacedConvexShape Second>
[[nodiscard]] constexpr auto
collide_convex_general(const First& first,
                       const Transform& first_pose,
                       const Second& second,
                       const Transform& second_pose,
                       const CollisionSettings& settings) noexcept -> std::optional<ContactManifold>
{
    const GjkResult closest = gjk(first, first_pose, second, second_pose);
    const float margins = first.margin() + second.margin();
    if (closest.status == GjkStatus::SEPARATED)
    {
        if (closest.distance - margins > settings.speculative_distance)
        {
            return std::nullopt;
        }
        if (closest.distance > 1e-6F)
        {
            const Vector3 normal = (closest.point_second - closest.point_first) / closest.distance;
            return contact_manifold(first, first_pose, second, second_pose, normal, closest.point_first, closest.point_second, closest.distance, settings,
                                    ContactAlgorithm::GJK);
        }
    }
    const EpaResult deepest = epa(first, first_pose, second, second_pose, closest);
    return contact_manifold(first, first_pose, second, second_pose, deepest.normal, deepest.point_first, deepest.point_second, -deepest.depth, settings,
                            ContactAlgorithm::EPA);
}

/// @brief The contact manifold of @p first and @p second at their poses (normal from first to second), or
///        std::nullopt when they are farther apart than the speculative distance.
template<FacedConvexShape First, FacedConvexShape Second>
[[nodiscard]] constexpr auto
collide_convex(const First& first,
               const Transform& first_pose,
               const Second& second,
               const Transform& second_pose,
               const CollisionSettings& settings = {}) noexcept -> std::optional<ContactManifold>
{
    // The analytic pairs in either order (the result flipped for the reversed order).
    if constexpr (std::same_as<First, SphereShape> && std::same_as<Second, SphereShape>)
    {
        return collide_spheres(first, first_pose, second, second_pose, settings);
    }
    else if constexpr (std::same_as<First, SphereShape> && std::same_as<Second, CapsuleShape>)
    {
        return collide_sphere_capsule(first, first_pose, second, second_pose, settings);
    }
    else if constexpr (std::same_as<First, CapsuleShape> && std::same_as<Second, SphereShape>)
    {
        return convex_detail::reversed(collide_sphere_capsule(second, second_pose, first, first_pose, settings));
    }
    else if constexpr (std::same_as<First, CapsuleShape> && std::same_as<Second, CapsuleShape>)
    {
        return collide_capsules(first, first_pose, second, second_pose, settings);
    }
    else if constexpr (std::same_as<First, SphereShape> && std::same_as<Second, BoxShape>)
    {
        return collide_sphere_box(first, first_pose, second, second_pose, settings);
    }
    else if constexpr (std::same_as<First, BoxShape> && std::same_as<Second, SphereShape>)
    {
        return convex_detail::reversed(collide_sphere_box(second, second_pose, first, first_pose, settings));
    }
    else if constexpr (std::same_as<First, BoxShape> && std::same_as<Second, BoxShape>)
    {
        return collide_separating_axes(first, first_pose, second, second_pose, settings, ContactAlgorithm::BOX_BOX);
    }
    else if constexpr (std::same_as<First, TriangleShape> && std::same_as<Second, TriangleShape>)
    {
        return collide_separating_axes(first, first_pose, second, second_pose, settings, ContactAlgorithm::TRIANGLE_TRIANGLE);
    }
    else
    {
        return collide_convex_general(first, first_pose, second, second_pose, settings);
    }
}

/// @brief The distance between @p first and @p second at their poses: GJK between the cores minus the margins, EPA's
///        depth (negative) when the cores overlap.
template<ConvexShape First, ConvexShape Second>
[[nodiscard]] constexpr auto
convex_distance(const First& first,
                const Transform& first_pose,
                const Second& second,
                const Transform& second_pose) noexcept -> ShapeDistance
{
    const GjkResult closest = gjk(first, first_pose, second, second_pose);
    const float first_margin = first.margin();
    const float second_margin = second.margin();
    if (closest.status == GjkStatus::SEPARATED && closest.distance > 1e-6F)
    {
        const Vector3 normal = (closest.point_second - closest.point_first) / closest.distance;
        return {.distance = closest.distance - first_margin - second_margin,
                .point_first = closest.point_first + normal * first_margin,
                .point_second = closest.point_second - normal * second_margin,
                .normal = normal};
    }
    const EpaResult deepest = epa(first, first_pose, second, second_pose, closest);
    return {.distance = -deepest.depth - first_margin - second_margin,
            .point_first = deepest.point_first + deepest.normal * first_margin,
            .point_second = deepest.point_second - deepest.normal * second_margin,
            .normal = deepest.normal};
}

} // namespace gfx::physics
